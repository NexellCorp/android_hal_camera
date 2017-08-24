#define LOG_TAG "StreamManager"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <camera/Camera.h>
#include <hardware/camera3.h>

#include <libnxjpeg.h>

#include "v4l2.h"
#include "StreamManager.h"

namespace android {

#define TRACE_STREAM
#ifdef TRACE_STREAM
#define dbg_stream(a...)	ALOGD(a)
#else
#define dbg_stream(a...)
#endif

int StreamManager::registerRequest(camera3_capture_request_t *r)
{
	const camera3_stream_buffer_t *previewBuffer = NULL;
	const camera3_stream_buffer_t *recordBuffer = NULL;
	const camera3_stream_buffer_t *captureBuffer = NULL;

	dbg_stream("registerRequest --> num_output_buffers %d",
			   r->num_output_buffers);

	for (uint32_t i = 0; i < r->num_output_buffers; i++) {
		const camera3_stream_buffer_t *b = &r->output_buffers[i];
		if (b->status) {
			ALOGE("buffer status is not valid to use: 0x%x", b->status);
			return -EINVAL;
		}

		/* TODO: check acquire fence */

		private_handle_t *ph = (private_handle_t *)*b->buffer;
		if (ph->share_fd < 0) {
			ALOGE("Invalid Buffer--> no fd");
			return -EINVAL;
		}

		if (ph->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
			previewBuffer = b;
		} else if (ph->format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
			/* TODO: assume video */
			recordBuffer = b;
		} else {
			dbg_stream("this formate:0x%x might be for video snapshot",
					ph->format);
			dbg_stream("width:%d, height:%d, size:%d",
					ph->width, ph->height, ph->size);
			captureBuffer = b;
		}
	}

	if (previewBuffer != NULL) {
		NXCamera3Buffer *buf = mFQ.dequeue();
		if (!buf) {
			ALOGE("Failed to dequeue NXCamera3Buffer from mFQ");
			return -ENOMEM;
		}

		if (recordBuffer == NULL)
				buf->init(r->frame_number, previewBuffer->stream,
					  previewBuffer->buffer);
		else {
			if (captureBuffer == NULL)
				buf->init(r->frame_number,
					  previewBuffer->stream, previewBuffer->buffer,
					  recordBuffer->stream, recordBuffer->buffer);
			else
				buf->init(r->frame_number,
					  previewBuffer->stream, previewBuffer->buffer,
					  recordBuffer->stream, recordBuffer->buffer,
					  captureBuffer->stream, captureBuffer->buffer);
		}

		CameraMetadata meta;
		meta = r->settings;
		if (meta.exists(ANDROID_CONTROL_AF_TRIGGER)) {
			dbg_stream("This frame has trigger info");
			buf->setTrigger(true);
		}

		dbg_stream("CREATE FN ---> %d", buf->getFrameNumber());
		dbg_stream("mQ.queue: %p", buf);
		mQ.queue(buf);

		// if (!isRunning() && mQ.size() > 0)
		if (!isRunning()) {
			ALOGD("START Camera3PreviewThread");
			run(String8::format("Camera3PreviewThread"));
		}
	}
	
	return 0;
}

status_t StreamManager::readyToRun()
{
	NXCamera3Buffer *buf;

	buf = mQ.getHead();
	private_handle_t *ph = buf->getPrivateHandle(PREVIEW_STREAM);
	int ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("Failed to setBufferFormat");
		return ret;
	}

	ret = v4l2_req_buf(mFd, MAX_BUFFER_COUNT);
	if (ret) {
		ALOGE("Failed to req buf : %d\n", ret);
		return ret;
	}

	size_t bufferCount = mQ.size();;
	for (size_t i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		dbg_stream("mQ.dequeue: %p", buf);
		if (!buf) {
			ALOGE("FATAL ERROR: Check Q!!!");
			return -EINVAL;
		}

		int dma_fd = buf->getDmaFd(PREVIEW_STREAM);
		ret = v4l2_qbuf(mFd, i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("Failed to v4l2_qbuf for preview(index: %zu)", i);
			return ret;
		}

		mRQ.queue(buf);
		dbg_stream("mRQ.queue: %p", buf);
	}
	setQIndex(bufferCount);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("Failed to stream on:%d", ret);
		return ret;
	}

	return NO_ERROR;
}

bool StreamManager::threadLoop()
{
	int dqIndex;
	int fd;
	int ret;

	dbg_stream("[LOOP] mQ %zu, mRQ %zu", mQ.size(), mRQ.size());
	if (mRQ.size() > 0) {
		ret = v4l2_dqbuf(mFd, &dqIndex, &fd, 1);
		if (ret) {
			ALOGE("Failed to dqbuf for preview");
			return false;
		}
		dbg_stream("dqIndex %d", dqIndex);

		ret = sendResult();
		if (ret) {
			ALOGE("Failed to sendResult: %d", ret);
			return false;
		}
	}

	int qSize = mQ.size();
	if (qSize > 0) {
		for (int i = 0; i < qSize; i++) {
			NXCamera3Buffer *buf = mQ.dequeue();
			dbg_stream("mQ.dequeue: %p", buf);
			int dma_fd = buf->getDmaFd(PREVIEW_STREAM);
			ret = v4l2_qbuf(mFd, mQIndex, &dma_fd, 1, &mSize);
			if (ret) {
				ALOGE("Failed to qbuf index %d", mQIndex);
				return false;
			}
			dbg_stream("qbuf index %d", mQIndex);
			mRQ.queue(buf);
			setQIndex(mQIndex+1);
		}
	} else {
		dbg_stream("=======================> underflow of input");
		dbg_stream("InputSize: %zu, QueuedSize: %zu", mQ.size(), mRQ.size());
		if (mQ.size() == 0 && mRQ.size() == 0) {
			ALOGD("NO BUFFER.... wait stopping");
			usleep(10000);
		}
	}

	return true;
}

int StreamManager::setBufferFormat(private_handle_t *buf)
{
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;

	ALOGD("[%s] fd:%d, ion_hnd:%p, size:%d, width:%d, height:%d, stride:%d",
		  __func__, buf->share_fd, buf->ion_hnd, buf->size,
		  buf->width, buf->height, buf->stride);

	int ret = module->lock_ycbcr(module, buf, PROT_READ | PROT_WRITE, 0, 0,
				     buf->width, buf->height, &ycbcr);
	if (ret) {
		ALOGE("failed to lock_ycbcr for the buf - %d", buf->share_fd);
		return -EINVAL;
	}

	uint32_t num_planes = 3;
	uint32_t y_width = buf->width;
	uint32_t y_height = buf->height;
	uint32_t strides[num_planes];
	uint32_t sizes[num_planes];

	ALOGD("[%s] ystride:%zu, cstride:%zu", __func__, ycbcr.ystride,
	      ycbcr.cstride);

	strides[0] = (uint32_t)ycbcr.ystride;
	sizes[0] = (uint64_t)(ycbcr.cb) - (uint64_t)(ycbcr.y);
	strides[1] = strides[2] = (uint32_t)ycbcr.cstride;
	sizes[1] = sizes[2] = (uint64_t)ycbcr.cr - (uint64_t)ycbcr.cb;

	mSize = 0;
	for (uint32_t i = 0; i < num_planes; i++) {
		ALOGD("[%d] mstrides = %d, sizes = %d\n",
		      i, strides[i], sizes[i]);
		mSize += sizes[i];
	}
	if (mSize != (uint32_t)buf->size) {
	    ALOGE("[%s] invalid size:%d\n", __FUNCTION__, mSize);
	    return -EINVAL;
	}

	ret = v4l2_set_format(mFd, buf->width, buf->height, num_planes,
						  strides, sizes);
	if (ret) {
		ALOGE("failed to set format : %d\n", ret);
		return ret;
	}

	ret = module->unlock(module, buf);
	if (ret) {
		ALOGE("[%s] failed to gralloc unlock:%d\n", __FUNCTION__, ret);
		return ret;
	}

	return 0;
}

int StreamManager::sendResult(bool drain)
{
	dbg_stream("sendResult: E");
	NXCamera3Buffer *buf = mRQ.dequeue();
	dbg_stream("mRQ.dequeue: %p", buf);
	if (!buf) {
		ALOGE("[SendResult] FATAL ERROR --> CHECK RQ");
		return -EINVAL;
	}

	if (mPrevFrameNumber != 0) {
		if (buf->getFrameNumber() != (mPrevFrameNumber + 1)) {
			ALOGE("FATAL ERROR --> frame number is abnormal(%d/%d)",
				  buf->getFrameNumber(), mPrevFrameNumber + 1);
			buf->setFrameNumber(mPrevFrameNumber + 1);
		}
	}

	/* notify */
	nsecs_t	timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	camera3_notify_msg_t msg;
	memset(&msg, 0x0, sizeof(camera3_notify_msg_t));
	msg.type = CAMERA3_MSG_SHUTTER;
	msg.message.shutter.frame_number = buf->getFrameNumber();
	msg.message.shutter.timestamp = timestamp;
	mCallbacks->notify(mCallbacks, &msg);

	/* send result */
	camera3_capture_result_t result;
	CameraMetadata metaData;

	bzero(&result, sizeof(camera3_capture_result_t));

	result.frame_number = buf->getFrameNumber();
	result.num_output_buffers = 1;

	camera3_stream_buffer_t output_buffer[MAX_STREAM];
	output_buffer[0].stream = buf->getStream(PREVIEW_STREAM);
	output_buffer[0].buffer = buf->getBuffer(PREVIEW_STREAM);
	output_buffer[0].release_fence = -1;
	output_buffer[0].acquire_fence = -1;
	output_buffer[0].status = 0;

	if (buf->getStream(RECORD_STREAM) != NULL) {
		if (!drain) {
			copyBuffer(buf->getPrivateHandle(RECORD_STREAM),
					buf->getPrivateHandle(PREVIEW_STREAM));
		}
		result.num_output_buffers++;
		output_buffer[1].stream = buf->getStream(RECORD_STREAM);
		output_buffer[1].buffer = buf->getBuffer(RECORD_STREAM);
		output_buffer[1].release_fence = -1;
		output_buffer[1].acquire_fence = -1;
		output_buffer[1].status = 0;

		if (buf->getStream(CAPTURE_STREAM) != NULL) {
			dbg_stream("jpeg encoding for video snapshot:%d", drain);
			if (!drain)
				jpegEncoding(buf->getPrivateHandle(CAPTURE_STREAM),
						buf->getPrivateHandle(RECORD_STREAM));
			result.num_output_buffers++;
			output_buffer[2].stream = buf->getStream(CAPTURE_STREAM);
			output_buffer[2].buffer = buf->getBuffer(CAPTURE_STREAM);
			output_buffer[2].release_fence = -1;
			output_buffer[2].acquire_fence = -1;
			output_buffer[2].status = 0;
		}
	}

	result.output_buffers = output_buffer;

	metaData.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
	if (buf->haveTrigger()) {
		uint8_t ae_state = (uint8_t)ANDROID_CONTROL_AE_STATE_CONVERGED;
		metaData.update(ANDROID_CONTROL_AE_STATE, &ae_state, 1);
		uint8_t afState = (uint8_t)ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	result.result = metaData.release();

	/*
         * If we set 'ANDROID_REQUEST_PARTIAL_RESULT_COUNT' to metadata
         * pratial_result should be same as the value
         * or bigger than the value
         * but not '0' for the case that result have metadata
         */
	result.partial_result = 1;

	result.input_buffer = NULL;

	dbg_stream("RETURN FN ---> %d", buf->getFrameNumber());
	mCallbacks->process_capture_result(mCallbacks, &result);

	mPrevFrameNumber = buf->getFrameNumber();
	mFQ.queue(buf);

	dbg_stream("sendResult: X");
	return 0;
}

void StreamManager::stopStreaming()
{
	dbg_stream("stopStreaming: E");
	// wait until all buffer drained
	while (!mQ.isEmpty() || !mRQ.isEmpty()) {
		ALOGD("Wait buffer drained");
		usleep(1000);
	}

	if (isRunning()) {
		dbg_stream("%s --> requestExitAndWait E", __func__);
		requestExitAndWait();
		dbg_stream("%s --> requestExitAndWait X", __func__);
	}

	stopV4l2();
	// drainBuffer();

	/* check last buffer */
	// usleep(100000); // wait 100ms
	// drainBuffer();
	dbg_stream("stopStreaming: X");
}

void StreamManager::stopV4l2()
{
	dbg_stream("stopV4l2: E");

	int ret = v4l2_streamoff(mFd);
	if (ret)
		ALOGE("Failed to v4l2_streamoff");

	ret = v4l2_req_buf(mFd, 0);
	if (ret)
		ALOGE("Failed to req buf(line: %d): %d\n", __LINE__, ret);

	mQIndex = 0;

	dbg_stream("stopV4l2: X");
}

void StreamManager::drainBuffer()
{
	dbg_stream("start draining all RQ buffers");

	// Mutex::Autolock l(mLock);

	while (!mQ.isEmpty())
		mRQ.queue(mQ.dequeue());

	while (!mRQ.isEmpty())
		sendResult(true);

	dbg_stream("end draining");
}

int StreamManager::jpegEncoding(private_handle_t *dst, private_handle_t *src)
{
	android_ycbcr srcY;
	void *dstY;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	dbg_stream("start jpegEncoding");

	dstY = mmap(0, dst->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			dst->share_fd, 0);
	if (MAP_FAILED == dstY) {
		ALOGE("failed to mmap for dstY");
		return -EINVAL;
	}

	int ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
					src->width, src->height, &srcY);
	if (ret) {
		ALOGE("Failed to lock for src");
		module->unlock(module, dst);
		return ret;
	}

	dbg_stream("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
			   dstY, dst->size);

	int jpegSize;
	int jpegBufSize;
	char *jpegBuf;
	camera3_jpeg_blob_t *jpegBlob;
	unsigned char *planar[3];
	planar[0] = (unsigned char*)srcY.y;
	planar[1] = (unsigned char*)srcY.cb;
	planar[2] = (unsigned char*)srcY.cr;
	jpegSize = NX_JpegEncoding((unsigned char *)dstY, dst->size,
					(unsigned char const *)planar, src->width,
					src->height, srcY.ystride, srcY.cstride, 100,
					NX_PIXFORMAT_YUV420);
	if (jpegSize <= 0) {
		ALOGE("failed to NX_JpegEncoding!!!");
		ret = -EINVAL;
		goto unlock;
	}

	jpegBufSize = dst->size;
	jpegBuf = (char *)dstY;
	jpegBlob = (camera3_jpeg_blob_t *)(&jpegBuf[jpegBufSize -
						sizeof(camera3_jpeg_blob_t)]);
	jpegBlob->jpeg_size = jpegSize;
	jpegBlob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
	ALOGD("capture success: jpegSize(%d), totalSize(%d)",
		jpegSize, jpegBlob->jpeg_size);

	ret = 0;

unlock:
	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("Failed to gralloc unlock for dst:%d\n", ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("Failed to gralloc unlock for src:%d\n", ret);
		return ret;
	}

	dbg_stream("end jpegEncoding");
	return ret;
}

int StreamManager::copyBuffer(private_handle_t *dst, private_handle_t *src)
{
	android_ycbcr dstY, srcY;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	int ret = module->lock_ycbcr(module, dst, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
						   dst->width, dst->height, &dstY);
	if (ret) {
		ALOGE("Failed to lock for dst");
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
							 src->width, src->height, &srcY);
	if (ret) {
		ALOGE("Failed to lock for src");
		return ret;
	}

	dbg_stream("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
			   dstY.y, dst->size);

	if ((src->width == dst->width) && (src->height == dst->height)) {
		if ((srcY.ystride == dstY.ystride) && (srcY.cstride == dstY.cstride))
			memcpy(dstY.y, srcY.y, src->size);
		else {
			dbg_stream("src and dst buffer has a different alingment");
			for (int i = 0; i < src->height; i++) {
				unsigned long srcOffset = i * srcY.ystride;
				unsigned long srcCbCrOffset = (i/2) * srcY.cstride;
				unsigned long dstOffset = i * dstY.ystride;
				unsigned long dstCbCrOffset = (i/2) * dstY.cstride;
				memcpy((void*)((unsigned long)dstY.y + dstOffset),
					(void*)((unsigned long)srcY.y + srcOffset),
					dstY.ystride);
				if (i%2 == 0) {
					memcpy((void*)((unsigned long)dstY.cb + dstCbCrOffset),
						(void*)((unsigned long)srcY.cb + srcCbCrOffset),
						dstY.cstride);
					memcpy((void*)((unsigned long)dstY.cr + dstCbCrOffset),
						(void*)((unsigned long)srcY.cr + srcCbCrOffset),
						dstY.cstride);
				}
			}
		}
	}

	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("Failed to gralloc unlock for dst:%d\n", ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("Failed to gralloc unlock for src:%d\n", ret);
		return ret;
	}

	return 0;
}

}; // namespace android
