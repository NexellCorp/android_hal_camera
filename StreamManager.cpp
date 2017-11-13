#define LOG_TAG "StreamManager"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <camera/Camera.h>
#include <hardware/camera3.h>

#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>

#include <nx-scaler.h>
#include "v4l2.h"
#include "Exif.h"
#include "ExifProcessor.h"
#include "StreamManager.h"

namespace android {

//#define TRACE_STREAM
#ifdef TRACE_STREAM
#define dbg_stream(a...)	ALOGD(a)
#else
#define dbg_stream(a...)
#endif

int StreamManager::allocBuffer(uint32_t w, uint32_t h, uint32_t format,
			       buffer_handle_t **p)
{
	if (mAllocator == NULL) {
		ALOGE("Allocator is NULL");
		return -ENODEV;
	}

	hw_device_t *dev = NULL;
	alloc_device_t *device = NULL;
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	int ret = 0, stride= 0;
	buffer_handle_t *pHandle;

	pHandle = (buffer_handle_t *)malloc(sizeof(buffer_handle_t));
	if (pHandle == NULL)
		return -ENOMEM;

	ret = mAllocator->alloc(mAllocator, w, h, format,
			    PROT_READ | PROT_WRITE, pHandle, &stride);
	if (ret) {
		ALOGE("Failed to alloc a buffer:%d", ret);
		return -EINVAL;
	}

	*p = pHandle;

	return 0;
}

int StreamManager::scaling(private_handle_t *srcBuf,
			   const camera_metadata_t *request)
{
	CameraMetadata meta;
	CameraMetadata metaData;
	uint32_t cropX;
	uint32_t cropY;
	uint32_t cropWidth;
	uint32_t cropHeight;

	meta  = request;

	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		cropX = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		cropY = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		cropWidth = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		cropHeight = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		dbg_stream("CROP: left:%d, top:%d, width:%d, height:%d",
			   cropX, cropY, cropWidth, cropHeight);
	} else {
		cropX = 0;
		cropY = 0;
		cropWidth = srcBuf->width;
		cropHeight = srcBuf->height;
	}
/*
	if (((cropWidth - cropX) == srcBuf->width) &&
	    ((cropHeight - cropY) == srcBuf->height))
		return true;
*/
	if (mScalerFd < 0) {
		ALOGE("scaler fd is invalid");
		return -ENODEV;
	}

	ALOGD("[%s] start scaling", __func__);

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr src, dst;
	private_handle_t *dstBuf = NULL;
	struct nx_scaler_context ctx;
	int ret = 0;

	dstBuf = (private_handle_t*)*mScaleBuffer;
	ret = module->lock_ycbcr(module, srcBuf, PROT_READ | PROT_WRITE, 0, 9,
				 srcBuf->width, srcBuf->height, &src);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", srcBuf->share_fd);
		goto exit;
	}

	ret = module->lock_ycbcr(module, dstBuf, PROT_READ | PROT_WRITE, 0, 9,
				 dstBuf->width, dstBuf->height, &dst);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", dstBuf->share_fd);
		goto exit;
	}

	ctx.crop.x = cropX;
	ctx.crop.y = cropY;
	ctx.crop.width = cropWidth;
	ctx.crop.height = cropHeight;

	ctx.src_plane_num = 1;
	ctx.src_width = srcBuf->width;
	ctx.src_height = srcBuf->height;
	ctx.src_code = MEDIA_BUS_FMT_YUYV8_2X8;
	ctx.src_fds[0] = srcBuf->share_fd;
	ctx.src_stride[0] = src.ystride;
	ctx.src_stride[1] = src.cstride;
	ctx.src_stride[2] = src.cstride;

	ctx.dst_plane_num = 1;
	ctx.dst_width = dstBuf->width;
	ctx.dst_height = dstBuf->height;
	ctx.dst_code = MEDIA_BUS_FMT_YUYV8_2X8;
	ctx.dst_fds[0] = dstBuf->share_fd;
	ctx.dst_stride[0] = dst.ystride;
	ctx.dst_stride[1] = dst.cstride;
	ctx.dst_stride[2] = dst.cstride;

	ret = nx_scaler_run(mScalerFd, &ctx);
	if (ret < 0) {
		ALOGE("[%s] Failed to scaler set & run ioctl", __func__);
		goto unlock;
	}
	memcpy(src.y, dst.y, srcBuf->size);

unlock:
	if (module ) {
		ret = module->unlock(module, srcBuf);
		if (ret)
			ALOGE("[%s] Failed to gralloc unlock for srcBuf:%d",
			      __func__, ret);
		ret = module->unlock(module, dstBuf);
		if (ret)
			ALOGE("[%s] Failed to gralloc unlock for dstBuf:%d",
			      __func__, ret);
	}

exit:
	return ret;
}

int StreamManager::registerRequest(camera3_capture_request_t *r)
{
	const camera3_stream_buffer_t *previewBuffer = NULL;
	const camera3_stream_buffer_t *recordBuffer = NULL;
	const camera3_stream_buffer_t *captureBuffer = NULL;
	camera3_stream_buffer_t *prev = NULL;

	dbg_stream("registerRequest --> num_output_buffers %d",
			   r->num_output_buffers);

	for (uint32_t i = 0; i < r->num_output_buffers; i++) {
		const camera3_stream_buffer_t *b = &r->output_buffers[i];

		if ((b == NULL) || (b->status)) {
			ALOGE("buffer status is not valid to use: 0x%x", b->status);
			return -EINVAL;
		}

		/* TODO: check acquire fence */

		private_handle_t *ph = (private_handle_t *)*b->buffer;
		if (ph->share_fd < 0) {
			ALOGE("Invalid Buffer--> no fd");
			return -EINVAL;
		}
		dbg_stream("format:%d, width:%d, height:%d, size:%d",
			   ph->format, ph->width, ph->height, ph->size);

		if (r->num_output_buffers == 1) {
			if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
				prev = (camera3_stream_buffer_t *)malloc(sizeof(camera3_stream_buffer_t));
				if (prev == NULL)
					return -ENOMEM;
				allocBuffer(ph->width, ph->height,
					    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
					    &prev->buffer);
				if (prev->buffer == NULL) {
					if (prev)
						free(prev);
					return -ENOMEM;
				}
				prev->stream = NULL;
				previewBuffer = (const camera3_stream_buffer_t *)prev;
				captureBuffer = b;
			} else
				previewBuffer = b;
		} else {
			if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
				dbg_stream("this format:0x%x might be for video snapshot or still capture",
					   ph->format);
				dbg_stream("width:%d, height:%d, size:%d",
					   ph->width, ph->height, ph->size);
				captureBuffer = b;
			} else {
				if (previewBuffer == NULL)
					previewBuffer = b;
				else if (recordBuffer == NULL)
					recordBuffer = b;
				else
					captureBuffer = b;
			}
		}
	}

	if (previewBuffer != NULL) {

		if (r->settings != NULL) {
			CameraMetadata request;
			request = r->settings;
			if (request.exists(ANDROID_REQUEST_ID)) {
				if (mPipeLineDepth == MAX_BUFFER_COUNT)
					mPipeLineDepth = 1;
				else
					mPipeLineDepth++;
			}
			mMetaInfo = request.release();
		}
		dbg_stream("frame_number:%d", r->frame_number);
		NXCamera3Buffer *buf = mFQ.dequeue();
		if (!buf) {
			ALOGE("Failed to dequeue NXCamera3Buffer from mFQ");
			if ((mAllocator) && (prev->buffer))
				mAllocator->free(mAllocator, (buffer_handle_t)*prev->buffer);
			if (prev)
				free(prev);
			return -ENOMEM;
		}

		if (recordBuffer == NULL) {
			if (captureBuffer == NULL)
				buf->init(r->frame_number, mMetaInfo, previewBuffer->stream,
					  previewBuffer->buffer);
			else
				buf->init(r->frame_number, mMetaInfo,
					  previewBuffer->stream, previewBuffer->buffer,
					  NULL, NULL,
					  captureBuffer->stream, captureBuffer->buffer);
		} else {
			if (captureBuffer == NULL)
				buf->init(r->frame_number, mMetaInfo,
					  previewBuffer->stream, previewBuffer->buffer,
					  recordBuffer->stream, recordBuffer->buffer);
			else
				buf->init(r->frame_number, mMetaInfo,
					  previewBuffer->stream, previewBuffer->buffer,
					  recordBuffer->stream, recordBuffer->buffer,
					  captureBuffer->stream, captureBuffer->buffer);
		}

		dbg_stream("CREATE FN ---> %d", buf->getFrameNumber());
		dbg_stream("mQ.queue: %p", buf);
		mQ.queue(buf);

		// if (!isRunning() && mQ.size() > 0)
		if (!isRunning()) {
			dbg_stream("START Camera3PreviewThread");
			run(String8::format("Camera3PreviewThread"));
		}
	} else
		ALOGE("buffer is NULL");

	if (prev)
		free(prev);

	return 0;
}

status_t StreamManager::readyToRun()
{
	NXCamera3Buffer *buf;
	size_t bufferCount;

	dbg_stream("%s", __func__);

	buf = mQ.getHead();
	private_handle_t *ph = buf->getPrivateHandle(PREVIEW_STREAM);
	int ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("Failed to setBufferFormat:%d, mFd:%d", ret, mFd);
		drainBuffer();
		return ret;
	}

#ifdef CAMERA_USE_ZOOM
	buffer_handle_t *buffer = NULL;
	buffer = (buffer_handle_t *)malloc(sizeof(buffer_handle_t));
	if (buffer == NULL)
		return -ENOMEM;
	allocBuffer(ph->width, ph->height, ph->format, &buffer);
	if (*buffer == NULL) {
		ALOGE("[%s] failed to alloc new Buffer for scaling", __func__);
		free(buffer);
		return -ENOMEM;
	}
	mScaleBuffer = buffer;
#endif

	ret = v4l2_req_buf(mFd, MAX_BUFFER_COUNT);
	if (ret) {
		ALOGE("Failed to req buf : %d, mFd:%d\n", ret, mFd);
		drainBuffer();
		return ret;
	}

	bufferCount = mQ.size();
	for (size_t i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		dbg_stream("mQ.dequeue: %p", buf);
		if (!buf) {
			ALOGE("Fail - FATAL ERROR: Check Q!!!");
			ret = -EINVAL;
			goto fail;
		}
		int dma_fd = buf->getDmaFd(PREVIEW_STREAM);
		ret = v4l2_qbuf(mFd, i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("Failed to v4l2_qbuf for preview(index: %zu)", i);
			goto fail;
		}

		mRQ.queue(buf);
		dbg_stream("mRQ.queue: %p", buf);
	}
	setQIndex(bufferCount);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("Failed to stream on:%d", ret);
		goto fail;
	}

	return NO_ERROR;

fail:
	dbg_stream("Failed to stream on:%d", ret);
	drainBuffer();
	ret = v4l2_req_buf(mFd, 0);
	if (ret) {
		ALOGE("Failed to req buf(line: %d): %d, mFd:%d\n", __LINE__, ret, mFd);
	}
	return ret;
}

bool StreamManager::threadLoop()
{
	int dqIndex;
	int fd;
	int ret;
	int qSize;

	dbg_stream("[LOOP] mQ %zu, mRQ %zu", mQ.size(), mRQ.size());
	if (mRQ.size() > 0) {
		ret = v4l2_dqbuf(mFd, &dqIndex, &fd, 1);
		if (ret) {
			ALOGE("Failed to dqbuf for preview:%d", ret);
			goto stop;
		}
		dbg_stream("dqIndex %d", dqIndex);

		ret = sendResult();
		if (ret) {
			ALOGE("Failed to sendResult: %d", ret);
			goto stop;
		}
	}

	qSize = mQ.size();
	if (qSize > 0) {
		for (int i = 0; i < qSize; i++) {
			NXCamera3Buffer *buf = mQ.dequeue();
			dbg_stream("mQ.dequeue: %p, mQIndex:%d", buf, mQIndex);
			int dma_fd = buf->getDmaFd(PREVIEW_STREAM);
			ret = v4l2_qbuf(mFd, mQIndex, &dma_fd, 1, &mSize);
			if (ret) {
				ALOGE("Failed to qbuf index %d, ret:%d, mFd:%d", mQIndex, ret, mFd);
				goto stop;
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

stop:
	dbg_stream("StreamManager thread is stopped");
	drainBuffer();
	stopV4l2();
	return false;
}

int StreamManager::setBufferFormat(private_handle_t *buf)
{
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;
	int ret;

	ALOGD("[%s] fd:%d, ion_hnd:%p, size:%d, width:%d, height:%d, stride:%d",
		  __func__, buf->share_fd, buf->ion_hnd, buf->size,
		  buf->width, buf->height, buf->stride);

	uint32_t num_planes = 3;
	uint32_t strides[num_planes];
	uint32_t sizes[num_planes];
	uint32_t f;
	mSize = 0;

	ret = module->lock_ycbcr(module, buf, PROT_READ | PROT_WRITE, 0, 0,
				 buf->width, buf->height, &ycbcr);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", buf->share_fd);
		return -EINVAL;
	}
	ALOGD("[%s] ystride:%zu, cstride:%zu", __func__, ycbcr.ystride,
	      ycbcr.cstride);

	strides[0] = (uint32_t)ycbcr.ystride;
	strides[1] = strides[2] = (uint32_t)ycbcr.cstride;
	if (buf->format == HAL_PIXEL_FORMAT_YV12) {
		sizes[0] = (uint64_t)(ycbcr.cr) - (uint64_t)(ycbcr.y);
		sizes[1] = sizes[2] = (uint64_t)ycbcr.cb - (uint64_t)ycbcr.cr;
		f = V4L2_PIX_FMT_YVU420;
		ALOGE("V4L2_PIX_FMT_YVU420");
	} else {
		sizes[0] = (uint64_t)(ycbcr.cb) - (uint64_t)(ycbcr.y);
		sizes[1] = sizes[2] = (uint64_t)ycbcr.cr - (uint64_t)ycbcr.cb;
		f = V4L2_PIX_FMT_YUV420;
	}
	for (uint32_t i = 0; i < num_planes; i++) {
		ALOGD("[%d] mstrides = %d, sizes = %d\n",
		      i, strides[i], sizes[i]);
		mSize += sizes[i];
	}

	if (mSize != (uint32_t)buf->size) {
	    ALOGE("[%s] invalid size:%d\n", __FUNCTION__, mSize);
	    return -EINVAL;
	}

	ret = v4l2_set_format(mFd, f, buf->width, buf->height,
			      num_planes, strides, sizes);
	if (ret) {
		ALOGE("Failed to set format : %d\n", ret);
		return ret;
	}

	if (module != NULL) {
		ret = module->unlock(module, buf);
		if (ret) {
			ALOGE("[%s] Failed to gralloc unlock:%d\n", __FUNCTION__, ret);
			return ret;
		}
	}

	return 0;
}

camera_metadata_t*
StreamManager::translateMetadata(const camera_metadata_t *request,
				 exif_attribute_t *mExif,
				 nsecs_t timestamp,
				 uint8_t pipeline_depth)
{
	CameraMetadata meta;
	camera_metadata_t *result;
	CameraMetadata metaData;

	meta = request;

	dbg_stream("[%s] timestamp:%ld, pipeline:%d", __func__,
		   timestamp, pipeline_depth);

	if (meta.exists(ANDROID_REQUEST_ID)) {
		int32_t request_id = meta.find(ANDROID_REQUEST_ID).data.i32[0];
		metaData.update(ANDROID_REQUEST_ID, &request_id, 1);
	}
	metaData.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
	metaData.update(ANDROID_REQUEST_PIPELINE_DEPTH, &pipeline_depth, 1);

	if (meta.exists(ANDROID_CONTROL_CAPTURE_INTENT)) {
		uint8_t capture_intent =
			meta.find(ANDROID_CONTROL_CAPTURE_INTENT).data.u8[0];
		dbg_stream("capture_intent:%d", capture_intent);
		metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);
	}

	if (meta.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
		int32_t fps_range[2];
		fps_range[0] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
		fps_range[1] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[1];
		dbg_stream("ANDROID_CONTROL_AE_TARGET_FPS_RANGE-min:%d,max:%d",
			   fps_range[0], fps_range[1]);
		metaData.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range, 2);
	}
	if (meta.exists(ANDROID_CONTROL_AF_TRIGGER) &&
	     meta.exists(ANDROID_CONTROL_AF_TRIGGER_ID)) {
		uint8_t trigger = meta.find(ANDROID_CONTROL_AF_TRIGGER).data.u8[0];
		int32_t trigger_id = meta.find(ANDROID_CONTROL_AF_TRIGGER_ID).data.i32[0];
		uint8_t afState;

		metaData.update(ANDROID_CONTROL_AF_TRIGGER, &trigger, 1);
		if (trigger == ANDROID_CONTROL_AF_TRIGGER_START) {
			dbg_stream("AF_TRIGGER_START");
			afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		} else if (trigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
			dbg_stream("AF_TRIGGER_CANCELL");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		} else {
			dbg_stream("AF_TRIGGER_IDLE");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		}
		dbg_stream("ANDROID_CONTROL_AF_STATE:%d", afState);
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AF_MODE)) {
		uint8_t afMode = meta.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
		uint8_t afState;
		dbg_stream("ANDROID_CONTROL_AF_MODE:%d", afMode);
		if ((afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE) ||
		    (afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO))
			afMode = ANDROID_CONTROL_AF_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
		if (afMode == ANDROID_CONTROL_AF_MODE_OFF)
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		else
			afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
		dbg_stream("ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION");
		int32_t expCompensation =
			meta.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
		metaData.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &expCompensation, 1);
	}
	if (meta.exists(ANDROID_CONTROL_MODE)) {
		uint8_t metaMode = meta.find(ANDROID_CONTROL_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_MODE:%d", metaMode);
		metaData.update(ANDROID_CONTROL_MODE, &metaMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_LOCK)) {
		uint8_t aeLock = meta.find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AE_LOCK:%d", aeLock);
		metaData.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_MODE)) {
		uint8_t aeMode = meta.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
		uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
		//uint8_t aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
		dbg_stream("ANDROID_CONTROL_AE_MODE:%d", aeMode);
		if (aeMode != ANDROID_CONTROL_AE_MODE_OFF)
			aeMode = ANDROID_CONTROL_AE_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
		metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
		uint8_t aeAntiBandingMode = meta.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AE_ANTIBANDING_MODE:%d", aeAntiBandingMode);
		metaData.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntiBandingMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER) &&
		meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_ID)) {
		uint8_t trigger = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER).data.u8[0];
		uint8_t trigger_id = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_ID).data.u8[0];
		uint8_t aeState;

		dbg_stream("ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER:%d, ID:%d", trigger, trigger_id);
		metaData.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &trigger, 1);
		if (trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
			aeState = ANDROID_CONTROL_AE_STATE_LOCKED;
			//uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
			metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		} else {
			aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
			metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		}
		dbg_stream("ANDROID_CONTROL_AE_STATE:%d", aeState);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_LOCK)) {
		uint8_t awbLock =
			meta.find(ANDROID_CONTROL_AWB_LOCK).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AWB_LOCK:%d", awbLock);
		metaData.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_MODE)) {
		uint8_t awbMode =
			meta.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AWB_MODE:%d", awbMode);
		if (awbMode != ANDROID_CONTROL_AWB_MODE_OFF)
			awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
		uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
		//uint8_t awbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
		dbg_stream("ANDROID_CONTROL_AWB_STATE:%d", awbState);
		metaData.update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);
		if (mExif)
			mExif->setWhiteBalance(awbMode);
	}
	if (meta.exists(ANDROID_CONTROL_SCENE_MODE)) {
		uint8_t sceneMode = meta.find(ANDROID_CONTROL_SCENE_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_SCENE_MODE:%d", sceneMode);
		metaData.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);
		if (mExif)
			mExif->setSceneCaptureType(sceneMode);
	}
	/*
	if (meta.exists(ANDROID_COLOR_CORRECTION_MODE)) {
		uint8_t colorCorrectMode =
			meta.find(ANDROID_COLOR_CORRECTION_MODE).data.u8[0];
		dbg_stream("ANDROID_COLOR_CORRECTION_MODE:%d", colorCorrectMode);
		metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectMode, 1);
	}
	*/
	if (meta.exists(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
		uint8_t colorCorrectAbeMode =
			meta.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE).data.u8[0];
		dbg_stream("ANDROID_COLOR_CORRECTION_ABERRATION_MODE:%d", colorCorrectAbeMode);
		metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectAbeMode, 1);
	}
	if (meta.exists(ANDROID_FLASH_MODE)) {
		uint8_t flashMode = meta.find(ANDROID_FLASH_MODE).data.u8[0];
		uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
		dbg_stream("ANDROID_FLASH_MODE:%d", flashMode);
		if (flashMode != ANDROID_FLASH_MODE_OFF)
			flashState = ANDROID_FLASH_STATE_READY;
		dbg_stream("ANDROID_FLASH_STATE:%d", flashState);
		metaData.update(ANDROID_FLASH_STATE, &flashState, 1);
		metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);
		if (mExif)
			mExif->setFlashMode(flashMode);
	}
	/*
	if (meta.exists(ANDROID_EDGE_MODE)) {
		uint8_t edgeMode = meta.find(ANDROID_EDGE_MODE).data.u8[0];
		dbg_stream("ANDROID_EDGE_MODE:%d", edgeMode);
		metaData.update(ANDROID_EDGE_MODE, &edgeMode, 1);
	}
	if (meta.exists(ANDROID_HOT_PIXEL_MODE)) {
		uint8_t hotPixelMode =
			meta.find(ANDROID_HOT_PIXEL_MODE).data.u8[0];
		dbg_stream("ANDROID_HOT_PIXEL_MODE:%d", hotPixelMode);
		metaData.update(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
	}
	*/
	if (meta.exists(ANDROID_LENS_FOCAL_LENGTH)) {
		float focalLength =
			meta.find(ANDROID_LENS_FOCAL_LENGTH).data.f[0];
		dbg_stream("ANDROID_LENS_FOCAL_LENGTH:%f", focalLength);
		metaData.update(ANDROID_LENS_FOCAL_LENGTH, &focalLength, 1);
		rational_t focal_length;
		focal_length.num = (uint32_t)(focalLength * EXIF_DEF_FOCAL_LEN_DEN);
		focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
		if (mExif)
			mExif->setFocalLength(focal_length);
	}
	if (meta.exists(ANDROID_LENS_OPTICAL_STABILIZATION_MODE)) {
		uint8_t optStabMode =
			meta.find(ANDROID_LENS_OPTICAL_STABILIZATION_MODE).data.u8[0];
		dbg_stream("ANDROID_LENS_OPTICAL_STABILIZATION_MODE:%d", optStabMode);
		metaData.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE)) {
		uint8_t vsMode = meta.find(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_VIDEO_STABILIZATION_MODE:%d", vsMode);
		metaData.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vsMode, 1);
	}
	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		int32_t scalerCropRegion[4];
		scalerCropRegion[0] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		scalerCropRegion[1] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		scalerCropRegion[2] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		scalerCropRegion[3] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		dbg_stream("ANDROID_SCALER_CROP_REGION:left-%d,top-%d,width-%d,height-%d",
			   scalerCropRegion[0], scalerCropRegion[1], scalerCropRegion[2],
			   scalerCropRegion[3]);
		metaData.update(ANDROID_SCALER_CROP_REGION, scalerCropRegion, 4);
		if (mExif)
			mExif->setCropResolution(scalerCropRegion[0], scalerCropRegion[1],
						 scalerCropRegion[2], scalerCropRegion[3]);
	} else {
		if (mExif)
			mExif->setCropResolution(0, 0, 0, 0);
	}

	if (meta.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
		 int64_t sensorExpTime =
			meta.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
		dbg_stream("ANDROID_SENSOR_EXPOSURE_TIME:%ld", sensorExpTime);
		metaData.update(ANDROID_SENSOR_EXPOSURE_TIME, &sensorExpTime, 1);
		if (mExif)
			mExif->setExposureTime(sensorExpTime);
	}
	if (meta.exists(ANDROID_SENSOR_FRAME_DURATION)) {
		int64_t sensorFrameDuration =
			meta.find(ANDROID_SENSOR_FRAME_DURATION).data.i64[0];
		int64_t minFrameDuration = 0;
		if (meta.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
			minFrameDuration = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
			dbg_stream("minFrame:%ld", minFrameDuration);
			minFrameDuration = (long) 1e9/ minFrameDuration;
		}
		dbg_stream("ANDROID_SENSOR_FRAME_DURATION:%ld, Min:%ld",
			   sensorFrameDuration, minFrameDuration);
		if (sensorFrameDuration < minFrameDuration)
			sensorFrameDuration = minFrameDuration;
		metaData.update(ANDROID_SENSOR_FRAME_DURATION, &sensorFrameDuration, 1);
	}
	/*
	if (meta.exists(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW)) {
		int64_t sensorRollingShutterSkew =
			meta.find(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW).data.i64[0];
		dbg_stream("ANDROID_SENSOR_ROLLING_SHUTTER_SKEW:%ld", sensorRollingShutterSkew);
		metaData.update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &sensorRollingShutterSkew, 1);
	}
	*/
	if (meta.exists(ANDROID_SHADING_MODE)) {
		uint8_t  shadingMode =
			meta.find(ANDROID_SHADING_MODE).data.u8[0];
		dbg_stream("ANDROID_SHADING_MODE:%d", shadingMode);
		metaData.update(ANDROID_SHADING_MODE, &shadingMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_FACE_DETECT_MODE)) {
		uint8_t fwk_facedetectMode =
			meta.find(ANDROID_STATISTICS_FACE_DETECT_MODE).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_FACE_DETECT_MODE:%d", fwk_facedetectMode);
		metaData.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &fwk_facedetectMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP)) {
		uint8_t sharpnessMapMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_LENS_SHADING_MAP:%d", sharpnessMapMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP, &sharpnessMapMode, 1);
	}
	#if 0
	if (meta.exists(ANDROID_COLOR_CORRECTION_GAINS)) {
		float colorCorrectGains[4];
		colorCorrectGains[0] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[0];
		colorCorrectGains[1] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[1];
		colorCorrectGains[2] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[2];
		colorCorrectGains[3] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[3];
		dbg_stream("ANDROID_COLOR_CORRECTION_GAINS-ColorGain:%f,%f,%f,%f",
			   colorCorrectGains[0], colorCorrectGains[1], colorCorrectGains[2],
			   colorCorrectGains[3]);
		metaData.update(ANDROID_COLOR_CORRECTION_GAINS, colorCorrectGains, 4);
	}
	if (meta.exists(ANDROID_COLOR_CORRECTION_TRANSFORM)) {
		camera_metadata_rational_t ccTransform[9];
		ccTransform[0].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[0].numerator;
		ccTransform[0].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[0].denominator;
		ccTransform[1].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[1].numerator;
		ccTransform[1].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[1].denominator;
		ccTransform[2].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[2].numerator;
		ccTransform[2].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[2].denominator;
		ccTransform[3].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[3].numerator;
		ccTransform[3].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[3].denominator;
		ccTransform[4].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[4].numerator;
		ccTransform[4].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[4].denominator;
		ccTransform[5].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[5].numerator;
		ccTransform[5].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[5].denominator;
		ccTransform[6].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[6].numerator;
		ccTransform[6].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[6].denominator;
		ccTransform[7].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[7].numerator;
		ccTransform[7].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[7].denominator;
		ccTransform[8].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[8].numerator;
		ccTransform[8].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[8].denominator;
		dbg_stream("ANDROID_COLOR_CORRECTION_TRANSFORM:%d/%d, %d/%d, %d/%d",
			   ccTransform[0].numerator/ccTransform[0].denominator,
			   ccTransform[1].numerator/ccTransform[1].denominator,
			   ccTransform[2].numerator/ccTransform[2].denominator,
			   ccTransform[3].numerator/ccTransform[0].denominator,
			   ccTransform[4].numerator/ccTransform[1].denominator,
			   ccTransform[5].numerator/ccTransform[2].denominator,
			   ccTransform[6].numerator/ccTransform[0].denominator,
			   ccTransform[7].numerator/ccTransform[1].denominator,
			   ccTransform[8].numerator/ccTransform[2].denominator);
		metaData.update(ANDROID_COLOR_CORRECTION_TRANSFORM, ccTransform, 9);
	}
	#endif
	if (meta.exists(ANDROID_CONTROL_EFFECT_MODE)) {
		uint8_t fwk_effectMode =
			meta.find(ANDROID_CONTROL_EFFECT_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_EFFECT_MODE:%d", fwk_effectMode);
		metaData.update(ANDROID_CONTROL_EFFECT_MODE, &fwk_effectMode, 1);
	}
	if (meta.exists(ANDROID_SENSOR_TEST_PATTERN_MODE)) {
		int32_t fwk_testPatternMode =
			meta.find(ANDROID_SENSOR_TEST_PATTERN_MODE).data.i32[0];
		dbg_stream("ANDROID_SENSOR_TEST_PATTERN_MODE:%d", fwk_testPatternMode);
		metaData.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &fwk_testPatternMode, 1);
	}
	if (meta.exists(ANDROID_JPEG_ORIENTATION)) {
		int32_t orientation =
			meta.find(ANDROID_JPEG_ORIENTATION).data.i32[0];
		dbg_stream("ANDROID_JPEG_ORIENTATION:%d", orientation);
		metaData.update(ANDROID_JPEG_ORIENTATION, &orientation, 1);
		int32_t exifOri;
		if (orientation == 90)
			exifOri = EXIF_ORIENTATION_90;
		else if(orientation == 180)
			exifOri = EXIF_ORIENTATION_180;
		else if(orientation == 270)
			exifOri = EXIF_ORIENTATION_270;
		else
			exifOri = EXIF_ORIENTATION_UP;
		if (mExif)
			mExif->setOrientation(exifOri);
	}
	if (meta.exists(ANDROID_JPEG_QUALITY)) {
		uint8_t quality =
			meta.find(ANDROID_JPEG_QUALITY).data.u8[0];
		dbg_stream("ANDROID_JPEG_QUALITY:%d", quality);
		metaData.update(ANDROID_JPEG_QUALITY, &quality, 1);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
		uint8_t thumb_quality =
			meta.find(ANDROID_JPEG_THUMBNAIL_QUALITY).data.u8[0];
		dbg_stream("ANDROID_JPEG_THUMBNAIL_QUALITY:%d", thumb_quality);
		metaData.update(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumb_quality, 1);
		if (mExif)
			mExif->setThumbnailQuality(thumb_quality);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
		int32_t size[2];
		size[0] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[0];
		size[1] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[1];
		dbg_stream("ANDROID_JPEG_THUMBNAIL_SIZE- width:%d, height:%d",
			   size[0], size[1]);
		metaData.update(ANDROID_JPEG_THUMBNAIL_SIZE, size, 2);
		if (mExif)
			mExif->setThumbResolution(size[0], size[1]);
	} else {
		if (mExif)
			mExif->setThumbResolution(0, 0);
	}

	if (meta.exists(ANDROID_JPEG_GPS_COORDINATES)) {
		double gps[3];
		gps[0] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[0];
		gps[1] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[1];
		gps[2] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[2];
		dbg_stream("ANDROID_JPEG_GPS_COORDINATES-%f:%f:%f", gps[0], gps[1], gps[2]);
		if (mExif)
			mExif->setGpsCoordinates(gps);
		metaData.update(ANDROID_JPEG_GPS_COORDINATES, gps, 3);
	} else {
		double gps[3];
		memset(gps, 0x0, sizeof(double) * 3);
		if (mExif)
			mExif->setGpsCoordinates(gps);
	}

	if (meta.exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
		dbg_stream("ANDROID_JPEG_GPS_PROCESSING_METHOD count:%d",
			   meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
		if (mExif)
			mExif->setGpsProcessingMethod(meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8,
						meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
		metaData.update(ANDROID_JPEG_GPS_PROCESSING_METHOD,
				meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8,
				meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
	}

	if (meta.exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
		int64_t timestamp = meta.find(ANDROID_JPEG_GPS_TIMESTAMP).data.i64[0];
		dbg_stream("ANDROID_JPEG_GPS_TIMESTAMP:%lld", timestamp);
		if (mExif)
			mExif->setGpsTimestamp(timestamp);
		metaData.update(ANDROID_JPEG_GPS_TIMESTAMP, &timestamp, 1);
	}

	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
		uint8_t shadingMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_LENS_SHADING_MAP_MODE:%d", shadingMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingMode, 1);
	}
	/*
	if (meta.exists(ANDROID_STATISTICS_SCENE_FLICKER)) {
		uint8_t sceneFlicker =
			meta.find(ANDROID_STATISTICS_SCENE_FLICKER).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_SCENE_FLICKER:%d", sceneFlicker);
		metaData.update(ANDROID_STATISTICS_SCENE_FLICKER, &sceneFlicker, 1);
	}
	*/
	result = metaData.release();

	return result;
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
	exif_attribute_t *exif = NULL;

	if ((buf->getStream(CAPTURE_STREAM) != NULL) &&
		(buf->getFormat(CAPTURE_STREAM) == HAL_PIXEL_FORMAT_BLOB)) {
		exif = new exif_attribute_t();
		if (exif == NULL) {
			ALOGD("[%s] Failed to make exif", __func__);
		}
	}

	bzero(&result, sizeof(camera3_capture_result_t));

	result.frame_number = buf->getFrameNumber();
	result.num_output_buffers = 0;
	result.result = translateMetadata(buf->getMetadata(),
					  exif, timestamp, mPipeLineDepth);

#ifdef CAMERA_USE_ZOOM
	if (buf->getStream(PREVIEW_STREAM) != NULL)
		scaling((private_handle_t*)*buf->getBuffer(PREVIEW_STREAM), buf->getMetadata());
#endif

	camera3_stream_buffer_t output_buffer[MAX_STREAM];
	uint32_t index = 0;

	if (buf->getStream(PREVIEW_STREAM) != NULL) {
		output_buffer[index].stream = buf->getStream(PREVIEW_STREAM);
		output_buffer[index].buffer = buf->getBuffer(PREVIEW_STREAM);
		output_buffer[index].release_fence = -1;
		output_buffer[index].acquire_fence = -1;
		output_buffer[index].status = 0;
		index++;
		result.num_output_buffers++;
	}

	if (buf->getStream(RECORD_STREAM) != NULL) {
		if (!drain) {
			copyBuffer(buf->getPrivateHandle(RECORD_STREAM),
				   buf->getPrivateHandle(PREVIEW_STREAM));
		}
		output_buffer[index].stream = buf->getStream(RECORD_STREAM);
		output_buffer[index].buffer = buf->getBuffer(RECORD_STREAM);
		output_buffer[index].release_fence = -1;
		output_buffer[index].acquire_fence = -1;
		output_buffer[index].status = 0;
		index++;
		result.num_output_buffers++;
	}

	if (buf->getStream(CAPTURE_STREAM) != NULL) {
		if (!drain) {
			if (buf->getFormat(CAPTURE_STREAM) == HAL_PIXEL_FORMAT_BLOB)
				jpegEncoding(buf->getPrivateHandle(CAPTURE_STREAM),
					     buf->getPrivateHandle(PREVIEW_STREAM), exif);
			else
				copyBuffer(buf->getPrivateHandle(CAPTURE_STREAM),
					   buf->getPrivateHandle(PREVIEW_STREAM));
		}
		output_buffer[index].stream = buf->getStream(CAPTURE_STREAM);
		output_buffer[index].buffer = buf->getBuffer(CAPTURE_STREAM);
		output_buffer[index].release_fence = -1;
		output_buffer[index].acquire_fence = -1;
		output_buffer[index].status = 0;
		index++;
		result.num_output_buffers++;
	}

	if (buf->getStream(PREVIEW_STREAM) == NULL) {
		mAllocator->free(mAllocator, (buffer_handle_t)*buf->getBuffer(PREVIEW_STREAM));
		free(buf->getBuffer(PREVIEW_STREAM));
	}

	result.output_buffers = output_buffer;
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

	if (exif)
		delete(exif);
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
		dbg_stream("[%s] --> requestExitAndWait E", __func__);
		requestExitAndWait();
		dbg_stream("[%s] --> requestExitAndWait X", __func__);
	}

	stopV4l2();
	if ((mAllocator) && (mScaleBuffer))
		mAllocator->free(mAllocator, (buffer_handle_t )*mScaleBuffer);
	if (mScaleBuffer)
		free(mScaleBuffer);
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
	int ret;

	dbg_stream("start draining all RQ buffers");

	// Mutex::Autolock l(mLock);

	while (!mQ.isEmpty())
		mRQ.queue(mQ.dequeue());

	while (!mRQ.isEmpty())
		sendResult(true);

	dbg_stream("end draining");
}

int StreamManager::jpegEncoding(private_handle_t *dst, private_handle_t *src, exif_attribute_t *exif)
{
	android_ycbcr srcY;
	void *dstV;
	int ret;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	dbg_stream("start jpegEncoding");

	ret = module->lock(module, dst, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
		   dst->width, dst->height, &dstV);
	if (ret) {
		ALOGE("failed to lock for dst");
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
				 src->width, src->height, &srcY);
	if (ret) {
		ALOGE("Failed to lock for src");
		module->unlock(module, dst);
		return ret;
	}

	dbg_stream("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
			   dstV, dst->size);

	/* make exif */
	ExifProcessor::ExifResult result =
		mExifProcessor.makeExif(mAllocator, src->width, src->height, src, exif, dst);
	mExifProcessor.clear();
	int exifSize = result.getSize();
	if (!exifSize) {
		ALOGE("Failed to make Exif");
		ret = -EINVAL;
		goto unlock;
	}
	dbg_stream("Exif size:%d", exifSize);

	int skipSOI;
	int jpegSize;
	int jpegBufSize;
	char *jpegBuf;
	camera3_jpeg_blob_t *jpegBlob;
	unsigned char *planar[3];
	planar[0] = (unsigned char*)srcY.y;
	planar[1] = (unsigned char*)srcY.cb;
	planar[2] = (unsigned char*)srcY.cr;
	jpegSize = NX_JpegEncoding((unsigned char *)dstV+exifSize, src->size,
				   (unsigned char const *)planar, src->width,
				   src->height, srcY.ystride, srcY.cstride, 100,
				   NX_PIXFORMAT_YUV420);
	if (jpegSize <= 0) {
		ALOGE("Failed to NX_JpegEncoding!!!");
		ret = -EINVAL;
		goto unlock;
	}
	if (exifSize)
		skipSOI = 2/*SOI*/;
	else
		skipSOI = 0;

	jpegSize = jpegSize - skipSOI;
	memcpy((unsigned char*)dstV+exifSize, (unsigned char*)dstV+exifSize + skipSOI, jpegSize);

	jpegBufSize = dst->size;
	jpegBuf = (char *) dstV;
	jpegBlob = (camera3_jpeg_blob_t *)(&jpegBuf[jpegBufSize -
						sizeof(camera3_jpeg_blob_t)]);
	jpegBlob->jpeg_size = jpegSize + exifSize;
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
		if ((srcY.cstride == dstY.cstride) && (srcY.cstride == dstY.cstride))
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
