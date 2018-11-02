#define LOG_TAG "NXStreamManager"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <sys/mman.h>

#include <hardware/camera.h>
#include <camera/CameraMetadata.h>

#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>

#include <nx-scaler.h>
#include "GlobalDef.h"
#include "metadata.h"
#include "StreamManager.h"

/*#define VERIFY_FRIST_FRAME*/
#ifdef VERIFY_FRIST_FRAME
buffer_handle_t firstFrame = NULL;
#endif

namespace android {

static gralloc_module_t const* getModule(void)
{
	hw_device_t *dev = NULL;
	alloc_device_t *device = NULL;
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	return module;
}

int StreamManager::allocBuffer(uint32_t w, uint32_t h, uint32_t format, buffer_handle_t *p)
{
	int ret = NO_ERROR, stride = 0;
	gralloc_module_t const *module = getModule();
	buffer_handle_t ph;

	if (!mAllocator) {
		ALOGE("[%s:%d] mAllocator is not exist", __func__, mCameraId);
		return -ENODEV;
	}
	ret = mAllocator->alloc(mAllocator, w, h, format,
			PROT_READ | PROT_WRITE, &ph, &stride);
	if (ret) {
		ALOGE("[%s:%d] Failed to alloc a new buffer:%d", __func__, mCameraId, ret);
		return -ENOMEM;
	}
	*p = ph;

	return ret;
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

	if (exif == NULL) {
		ALOGE("[%s:%d] Exif is NULL", __func__, mCameraId);
		return -EINVAL;
	}
	ALOGDD("[%s:%d]", __func__, mCameraId);

	ret = module->lock(module, dst, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			dst->width, dst->height, &dstV);
	if (ret) {
		ALOGE("[%s:%d] failed to lock for dst", __func__, mCameraId);
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			src->width, src->height, &srcY);
	if (ret) {
		ALOGE("[%s:%d] Failed to lock for src", __func__, mCameraId);
		module->unlock(module, dst);
		return ret;
	}
	ALOGDV("[%s:%d] src: %p(%d) --> dst: %p(%d)", __func__, mCameraId,
			srcY.y, src->size,
			dstV, dst->size);

	/* make exif */
	ExifProcessor::ExifResult result =
		mExifProcessor.makeExif(mAllocator, src->width, src->height, src, exif, dst);
	mExifProcessor.clear();
	int exifSize = result.getSize();
	if (!exifSize) {
		ALOGE("[%s:%d] Failed to make Exif", __func__, mCameraId);
		ret = -EINVAL;
		goto unlock;
	}
	ALOGDV("[%s:%d] Exif size:%d", __func__, mCameraId, exifSize);

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
		ALOGE("[%s:%d] Failed to NX_JpegEncoding!!!", __func__, mCameraId);
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
	ALOGDV("[%s:%d] capture success: jpegSize(%d), totalSize(%d)",
			__func__, mCameraId, jpegSize, jpegBlob->jpeg_size);
	ret = 0;

unlock:
	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("[%s:%d] Failed to gralloc unlock for dst:%d\n",
				__func__, mCameraId, ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("[%s:%d] Failed to gralloc unlock for src:%d\n",
				__func__, mCameraId, ret);
		return ret;
	}

	ALOGDD("[%s:%d] end jpegEncoding", __func__, mCameraId);
	return ret;
}

int StreamManager::scaling(private_handle_t *dstBuf, private_handle_t *srcBuf,
			const camera_metadata_t *request)
{
	if (!request)
		return -EINVAL;

	CameraMetadata meta;
	uint32_t cropX;
	uint32_t cropY;
	uint32_t cropWidth;
	uint32_t cropHeight;

	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		cropX = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		cropY = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		cropWidth = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		cropHeight = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		ALOGDV("[%s:%d] CROP: left:%d, top:%d, width:%d, height:%d",
				__func__, mCameraId,
				cropX, cropY, cropWidth, cropHeight);
	} else {
		cropX = 0;
		cropY = 0;
		cropWidth = srcBuf->width;
		cropHeight = srcBuf->height;
	}
	if (mScaler < 0) {
		ALOGE("[%s:%d] scaler fd", __func__, mCameraId);
		return -ENODEV;
	}
	ALOGDI("[%s:%d] scaling start, src %d * %d, dst %d * %d",
			__func__, mCameraId, srcBuf->width, srcBuf->height,
		dstBuf->width, dstBuf->height);

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = getModule();
	android_ycbcr src, dst;
	struct nx_scaler_context ctx;
	int ret = 0;

	ret = module->lock_ycbcr(module, srcBuf, PROT_READ | PROT_WRITE, 0, 9,
				 srcBuf->width, srcBuf->height, &src);
	if (ret) {
		ALOGE("[%s:%d] Failed to lock_ycbcr for the buf - %d",
				__func__, mCameraId, srcBuf->share_fd);
		goto exit;
	}

	ret = module->lock_ycbcr(module, dstBuf, PROT_READ | PROT_WRITE, 0, 9,
				 dstBuf->width, dstBuf->height, &dst);
	if (ret) {
		ALOGE("[%s:%d] Failed to lock_ycbcr for the buf - %d",
				__func__, mCameraId, dstBuf->share_fd);
		goto exit;
	}

	ctx.crop.x = cropX;
	ctx.crop.y = cropY;
	ctx.crop.width = cropWidth;
	ctx.crop.height = cropHeight;

	ctx.src_plane_num = 1;
	ctx.src_width = srcBuf->width;
	ctx.src_height = srcBuf->height;
	if (srcBuf->format == HAL_PIXEL_FORMAT_YV12)
		ctx.src_code = MEDIA_BUS_FMT_YVYU8_2X8;
	else
		ctx.src_code = MEDIA_BUS_FMT_YUYV8_2X8;
	ctx.src_fds[0] = srcBuf->share_fd;
	ctx.src_stride[0] = src.ystride;
	ctx.src_stride[1] = src.cstride;
	ctx.src_stride[2] = src.cstride;

	ctx.dst_plane_num = 1;
	ctx.dst_width = dstBuf->width;
	ctx.dst_height = dstBuf->height;
	if (dstBuf->format == HAL_PIXEL_FORMAT_YV12)
		ctx.dst_code = MEDIA_BUS_FMT_YVYU8_2X8;
	else
		ctx.dst_code = MEDIA_BUS_FMT_YUYV8_2X8;
	ctx.dst_fds[0] = dstBuf->share_fd;
	ctx.dst_stride[0] = dst.ystride;
	ctx.dst_stride[1] = dst.cstride;
	ctx.dst_stride[2] = dst.cstride;

	ret = nx_scaler_run(mScaler, &ctx);
	if (ret < 0) {
		ALOGE("[%s:%d] Failed to scaler set & run ioctl", __func__,
				mCameraId);
		goto unlock;
	}

unlock:
	if (module ) {
		ret = module->unlock(module, srcBuf);
		if (ret)
			ALOGE("[%s:%d] Failed to gralloc unlock for srcBuf:%d",
			      __func__, mCameraId, ret);
		ret = module->unlock(module, dstBuf);
		if (ret)
			ALOGE("[%s:%d] Failed to gralloc unlock for dstBuf:%d",
			      __func__, mCameraId, ret);
	}

exit:
	ALOGDV("[%s:%d] scaling done", __func__, mCameraId);
	meta.clear();
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
		ALOGE("[%s:%d] Failed to lock for dst", __func__, mCameraId);
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			src->width, src->height, &srcY);
	if (ret) {
		ALOGE("[%s:%d] Failed to lock for src", __func__, mCameraId);
		return ret;
	}

#ifdef VERIFY_FRIST_FRAME
	memset(dstY.y, 0x88, dst->size);
#endif
	ALOGDV("[%s:%d] src: %p(%d) --> dst: %p(%d)", __func__, mCameraId,
			srcY.y, src->size,
			dstY.y, dst->size);
	if ((src->width == dst->width) && (src->height == dst->height)) {
		if ((srcY.cstride == dstY.cstride) && (srcY.cstride == dstY.cstride)) {
			memcpy(dstY.y, srcY.y, src->size);
		} else {
			ALOGDD("[%s:%d] src and dst buffer has a different alingment",
					__func__, mCameraId);
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
	} else {
#ifdef VERIFY_FRIST_FRAME
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
#endif
		ret = module->unlock(module, dst);
		if (ret) {
			ALOGE("[%s:%d] Failed to gralloc unlock for dst:%d\n",
					__func__, mCameraId, ret);
			return ret;
		}
		ret = module->unlock(module, src);
		if (ret) {
			ALOGE("[%s:%d] Failed to gralloc unlock for src:%d\n",
					__func__, mCameraId, ret);
			return ret;
		}
		scaling(dst, src, 0);
		return 0;
	}
	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("[%s:%d] Failed to gralloc unlock for dst:%d\n",
				__func__, mCameraId, ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("[%s:%d] Failed to gralloc unlock for src:%d\n",
				__func__, mCameraId, ret);
		return ret;
	}

	return 0;
}

void StreamManager::setCaptureResult(uint32_t type, NXCamera3Buffer *buf)
{
	ALOGDV("[%s:%d] get result from %d frame, type:%d", __func__,
			mCameraId, buf->getFrameNumber(), type);

	if (type >= NX_MAX_STREAM)
		ALOGE("[%s:%d] Invalied type", __func__, mCameraId);
	else {
		mResultQ[type].queue(buf);
		if (!isRunning()) {
			ALOGDV("[%s:%d] START StreamManager Thread", __func__,
					mCameraId);
			run("StreamManagerThread");
		}
	}
}

void StreamManager::getCaptureResult(const struct nx_camera3_callback_ops *ops,
		uint32_t type,
		NXCamera3Buffer *buf)
{
	StreamManager *d = const_cast<StreamManager*>(static_cast<const StreamManager*>(ops->priv));
	d->setCaptureResult(type, buf);
}

int StreamManager::configureStreams(camera3_stream_configuration_t *stream_list)
{
	ALOGDD("[%s:%d] operation mode=%d", __func__, mCameraId,
			stream_list->operation_mode);

	for (uint32_t i = 0; i < stream_list->num_streams; i++) {
		camera3_stream_t *stream = stream_list->streams[i];
		ALOGDD("[%d:%zu] format:0x%x, width:%d, height:%d, usage:0x%x",
				mCameraId, i, stream->format, stream->width,
				stream->height, stream->usage);
		mStream[i] = new Stream(mCameraId, mFd[0], mScaler, mAllocator, &mResultCb, stream, i);
		if (mStream[i] == NULL) {
			ALOGE("[%s:%d] Failed to create stream:%d", __func__, mCameraId, i);
			return -EINVAL;
		}
		stream->priv = mStream[i]->getStream();
		ALOGDD("[%d: %zu] Mode:%d", mCameraId, i, mStream[i]->getMode());
	}

	return NO_ERROR;
}

int StreamManager::getRunningStreamsCount(void)
{
	int count = 0;

	for (int j = 0; j < NX_MAX_STREAM; j++)
	{
		if ((mStream[j] != NULL) && (mStream[j]->isRunning()) &&
				(!mStream[j]->getSkipFlag()))
				count++;
	}
	ALOGDV("[%s:%d] running streams count:%d", __func__, mCameraId, count);
	return count;
}

int StreamManager::runStreamThread(camera3_stream_t *s)
{
	int count = getRunningStreamsCount();
	Stream *stream = NULL;
	bool	skip = false;

	stream = (Stream *)s->priv;
	if (stream == NULL) {
		ALOGE("[%s:%d] Failed to get stream from buffer", __func__, mCameraId);
		return -EINVAL;
	}
	ALOGDV("[%s:%d] getRunningStreamsCount:%d", __func__, mCameraId, count);

	if (!count) {
		stream->setSkipFlag(skip);
		stream->setHandle(mFd[count]);
		if (!skip) {
			if (stream->skipFrames())
				goto fail;
		}
		if (stream->prepareForRun() == NO_ERROR)
			stream->run("Stream[%d] Thread", stream->getMode());
		else
			goto fail;
	} else {
		for (int j = 0; j < NX_MAX_STREAM; j++)
		{
			if ((mStream[j] != NULL) && (mStream[j]->isRunning())) {
				if ((mStream[j]->getWidth() == s->width) &&
					(mStream[j]->getHeight() == s->height)) {
#ifdef VERIFY_FRIST_FRAME
					mStream[j]->stopStreaming();
#endif
				} else {
					mStream[j]->stopStreaming();
				}
			}
		}
		count = getRunningStreamsCount();
		ALOGDV("[%s:%d] get current RunningStreamsCount:%d", __func__,
				mCameraId, count);
		if (mFd[count] < 0)
			skip = true;
		else
			stream->setHandle(mFd[count]);
		stream->setSkipFlag(skip);
		if (!skip) {
			if (stream->skipFrames())
				goto fail;
		}
		if (stream->prepareForRun() == NO_ERROR)
			stream->run("Stream[%d] Thread", stream->getMode());
		else
			goto fail;
	}
	return 0;

fail:
	ALOGE("[%s:%d] Failed to run stream[%d] thread", __func__,
			mCameraId, stream->getMode());
	return -EINVAL;
}

int StreamManager::registerRequests(camera3_capture_request_t *r)
{
	CameraMetadata setting;
	camera_metadata_t *meta = NULL;
	sp<Stream> stream;
	int ret = NO_ERROR;

	if (r->settings != NULL) {
		setting = r->settings;
	} else {
		ALOGDV("[%s:%d] metadata is null", __func__, mCameraId);
		setting = mMeta;
	}

	ALOGDD("[%s:%d] frame number:%d, num_output_buffers:%d", __func__,
			mCameraId, r->frame_number, r->num_output_buffers);

	if (setting.exists(ANDROID_REQUEST_ID)) {
		ALOGDI("[%s:%d] requestID:%d", __func__, mCameraId,
				setting.find(ANDROID_REQUEST_ID).data.i32[0]);
		if (mPipeLineDepth == MAX_BUFFER_COUNT)
			mPipeLineDepth = 1;
		else
			mPipeLineDepth++;
	} else
		ALOGDI("[%s:%d] ===> no request id", __func__, mCameraId);
	meta = setting.release();
	if (r->input_buffer != NULL) {
		const camera3_stream_buffer_t *b = r->input_buffer;
		if ((b == NULL) || (b->status)) {
			ALOGE("[%s:%d] input buffer or status is not valid to use:%d",
					__func__, mCameraId, b->status);
			return -EINVAL;
		}
		private_handle_t *ph = (private_handle_t *)*b->buffer;
		camera3_stream_t *s =b->stream;
		ALOGDD("[%s:%d] [Input] frmaeNumber:%d, format:0x%x, width:%d, height:%d, size:%d",
				__func__, mCameraId, r->frame_number, s->format, s->width,
				s->height, ph->size);
	}
	for (uint32_t i = 0; i < r->num_output_buffers; i++) {
		const camera3_stream_buffer_t *b = &r->output_buffers[i];

		if ((b == NULL) || (b->status)) {
			ALOGE("[%s:%d] buffer or status is not valid to use:%d",
					__func__, mCameraId, b->status);
			return -EINVAL;
		}

		private_handle_t *ph = (private_handle_t *)*b->buffer;
		if (ph->share_fd < 0) {
			ALOGE("[%s:%d] Invalid Buffer --> no fd", __func__, mCameraId);
			return -EINVAL;
		}
		ALOGDI("[%s:%d] [Output] frmaeNumber:%d, format:0x%x, width:%d, height:%d, size:%d",
				__func__, mCameraId, r->frame_number, ph->format,
				ph->width, ph->height, ph->size);
		Stream *s = (Stream *)(b->stream->priv);
		if (s == NULL) {
			ALOGE("[%s:%d] Failed to get stream for this buffer",
					__func__, mCameraId);
			return -EINVAL;
		}
		ret = s->registerBuffer(r->frame_number, b, meta);
		if (ret) {
			ALOGE("[%s:%d] Failed to register Buffer for buffer:%d",
					__func__, mCameraId, ret);
			return ret;
		}
		if (!s->isRunning()) {
			ret = runStreamThread(b->stream);
			if (ret)
				return ret;
		}
	}

	nx_camera_request_t *request = (nx_camera_request_t*)malloc(sizeof(nx_camera_request_t));
	if (!request) {
		ALOGE("[%s:%d] Failed to malloc for request", __func__, mCameraId);
		ret = -ENOMEM;
	}
	request->frame_number = r->frame_number;
	request->num_output_buffers = r->num_output_buffers;
	request->meta = meta;
	request->input_buffer = r->input_buffer;
	mRequestQ.queue(request);
	mMeta = meta;
	return ret;
out:
	return -EINVAL;
}

int StreamManager::stopStream()
{
	int ret = NO_ERROR, i;

	ALOGDD("[%s:%d] Enter", __func__, mCameraId);
	for (i = 0; i < NX_MAX_STREAM; i++) {
		if ((mStream[i] != NULL) && (mStream[i]->isRunning()))
			mStream[i]->stopStreaming();
	}
	ALOGDV("[%s:%d] mRequestQ:%d, mRQ:%d, mResultQ[%d:%d:%d:%d]",
			__func__, mCameraId, mRequestQ.size(), mRQ.size(), mResultQ[0].size(),
			mResultQ[1].size(), mResultQ[2].size(), mResultQ[3].size());

	while (!mRequestQ.isEmpty() || !mRQ.isEmpty()) {
		ALOGDV("[%s:%d] Wait buffer drained", __func__, mCameraId);
		usleep(1000);
	}

	for (i = 0; i < NX_MAX_STREAM; i++) {
		if (mStream[i] != NULL) {
			mStream[i].clear();
			mStream[i] = NULL;
		}
	}
#ifdef VERIFY_FRIST_FRAME
	if (mAllocator && firstFrame) {
		mAllocator->free(mAllocator, firstFrame);
		firstFrame = NULL;
	}
#endif
	ALOGDD("[%s:%d]", __func__, mCameraId);
	if (isRunning()) {
		ALOGDV("[%s:%d] requestExitAndWait Enter", __func__, mCameraId);
		requestExitAndWait();
		ALOGDV("[%s:%d] requestExitAndWait Exit", __func__, mCameraId);
	}
	return ret;
}

private_handle_t* StreamManager::getSimilarActiveStream(camera3_stream_buffer_t *out,
		int num_buffers, camera3_stream_t *s)
{
	private_handle_t *buf = NULL;
	for (int i = 0; i < num_buffers; i++) {
		camera3_stream_t *st = out[i].stream;
		Stream *stream = (Stream*)st->priv;
		if ((stream != NULL) && (stream->getFormat() != HAL_PIXEL_FORMAT_BLOB) &&
				(!stream->getSkipFlag()) &&
				(stream->getWidth() == s->width) &&
				(stream->getHeight() == s->height)) {
			buf = (private_handle_t*)*out[i].buffer;
			break;
		}
	}

	return buf;
}

int StreamManager::sendResult(void)
{
	int ret = NO_ERROR;

	nx_camera_request_t *request = mRequestQ.getHead();
	if (!request) {
		ALOGE("[%s:%d] Failed to get request from Queue",
				__func__, mCameraId);
		return -EINVAL;
	}

	/* notify */
	nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	/*ALOGDD("[Recorded file duration] [SENSOR] camera_id:%d - :%ld", mCameraId,
			nanoseconds_to_milliseconds(systemTime(SYSTEM_TIME_MONOTONIC)));*/
	camera3_notify_msg_t msg;
	memset(&msg, 0x0, sizeof(camera3_notify_msg_t));
	msg.type = CAMERA3_MSG_SHUTTER;
	msg.message.shutter.frame_number = request->frame_number;
	msg.message.shutter.timestamp = timestamp;
	mCb->notify(mCb, &msg);

	/* send result */
	private_handle_t *preview = NULL;
	camera3_capture_result_t result;
	exif_attribute_t *exif = NULL;
	bzero(&result, sizeof(camera3_capture_result_t));
	result.frame_number = request->frame_number;
	result.num_output_buffers = request->num_output_buffers;

	camera3_stream_buffer_t output_buffers[result.num_output_buffers];
	for (uint32_t i = 0; i < result.num_output_buffers; i++) {
		Stream *stream = NULL;
		NXCamera3Buffer *buf = mRQ.dequeue();
		if (buf) {
			output_buffers[i].stream = buf->getStream();
			output_buffers[i].buffer = buf->getBuffer();
			output_buffers[i].release_fence = -1;
			output_buffers[i].acquire_fence = -1;
			output_buffers[i].status = 0;
		} else {
			ALOGE("[%s:%d] Failed to get buffer form RQ", __func__, mCameraId);
			break;
		}

		stream = (Stream *)buf->getStream()->priv;
		if (stream != NULL) {
			if (!stream->getSkipFlag() &&
					(stream->getFormat() != HAL_PIXEL_FORMAT_BLOB)) {
				preview =
					(private_handle_t *)buf->getPrivateHandle();
			}
			if ((stream->getFormat() == HAL_PIXEL_FORMAT_BLOB) &&
					(exif == NULL)) {
					exif = new exif_attribute_t();
					if (!exif)
						ALOGE("[%s:%d] Failed to make exif",
								__func__, mCameraId);
			}
		} else
			ALOGDV("[%s:%d] setream is null\n", __func__, mCameraId);
	}
#ifdef VERIFY_FRIST_FRAME
	if (result.frame_number == 2) {
		private_handle_t *buffer = (private_handle_t *)*output_buffers[0].buffer;
		if (firstFrame == NULL) {
			allocBuffer(buffer->width, buffer->height, buffer->format, &firstFrame);
			if (firstFrame == NULL)
				ALOGE("[%s:%d] Failed to alloc a new buffer",
						__func__, mCameraId);
		}
		if (firstFrame) {
			ALOGDD("[%s:%d] ========> save first frame", __func__, mCameraId);
			copyBuffer((private_handle_t*)firstFrame, buffer);
		}
	}
	if (result.num_output_buffers == 2 && firstFrame) {
		private_handle_t *buffer = NULL;
		camera3_stream_t *s = NULL;
		ALOGDD("[%s:%d] =========> copy first frame", __func__, mCameraId);
		s = output_buffers[0].stream;
		if (s->usage & GRALLOC_USAGE_HW_TEXTURE)
			buffer = (private_handle_t *)*output_buffers[1].buffer;
		else
			buffer = (private_handle_t *)*output_buffers[0].buffer;
		copyBuffer(buffer, (private_handle_t*)firstFrame);
		if (mAllocator && firstFrame) {
			mAllocator->free(mAllocator, firstFrame);
			firstFrame = NULL;
		}
	}
#endif
	camera_metadata_t *meta = translateMetadata(mCameraId, request->meta, exif,
			timestamp, mPipeLineDepth);
	result.result = (const camera_metadata_t *)meta;
	if (preview != NULL) {
		for (uint32_t i = 0; i < result.num_output_buffers; i++) {
			camera3_stream_t *s = output_buffers[i].stream;
			Stream *stream = (Stream *)s->priv;
			private_handle_t *buffer = (private_handle_t *)*output_buffers[i].buffer;
			if (stream->getSkipFlag()) {
				private_handle_t *copy = getSimilarActiveStream(output_buffers,
						result.num_output_buffers, s);
				if (copy == NULL)
					copy = preview;
				uint32_t size[2] = {0, 0};
				if (stream->getFormat() == HAL_PIXEL_FORMAT_BLOB) {
					if ((stream->getWidth() != (uint32_t)copy->width) ||
						(stream->getHeight() != (uint32_t)copy->height)) {
						ALOGE("[%s:%d] Resolution is different", __func__,
								mCameraId);
					} else {
						ALOGD("[%s:%d] jpegEncoding", __func__, mCameraId);
						uint32_t crop[4] = {0, 0};
						if (stream->calScalingFactor(request->meta, crop))
							exif->setCropResolution(crop[0], crop[1], crop[2], crop[3]);
						else
							exif->setCropResolution(0, 0, buffer->width, buffer->height);
						jpegEncoding(buffer, copy, exif);
					}
				} else
					copyBuffer(buffer, copy);
			}
		}
	}
	result.output_buffers = output_buffers;
	result.partial_result = 1;
	result.input_buffer = request->input_buffer;
	ALOGDI("[%s:%d] frame_number:%d, num_output_buffers:%d", __func__,
			mCameraId, result.frame_number, result.num_output_buffers);

	mCb->process_capture_result(mCb, &result);
	request = mRequestQ.dequeue();
	if (request) {
		if (request->meta)
			free(request->meta);
		free(request);
	}
	ALOGDV("[%s:%d] Exit", __func__, mCameraId);
	free(meta);
	return 0;
}

void StreamManager::drainBuffer()
{
	int ret = NO_ERROR;

	ALOGDV("[%s:%d] Enter", __func__, mCameraId);

	while (!mRequestQ.isEmpty())
		sendResult();

	ALOGDV("[%s:%d] Exit", __func__, mCameraId);
}

status_t StreamManager::readyToRun()
{
	int ret = NO_ERROR;

	ALOGDV("[%s:%d]", __func__, mCameraId);
	return ret;
}

bool StreamManager::threadLoop()
{
	if (mRequestQ.size() > 0)
	{
		nx_camera_request_t *request = mRequestQ.getHead();
		if (!request) {
			ALOGE("[%s:%d] Failed to get request from Queue",
				__func__, mCameraId);
		return false;
	}
	uint32_t frame_number = request->frame_number;
	uint32_t num_buffers = request->num_output_buffers;
	if (mNumBuffers)
		num_buffers = mNumBuffers;
	for (int i = 0; i < NX_MAX_STREAM; i++) {
		int size = mResultQ[i].size();
		if (size > 0) {
			NXCamera3Buffer *buf =
			mResultQ[i].getHead();
			if ((buf) && (buf->getFrameNumber() == frame_number)) {
				ALOGDV("[%s:%d] got a buffer for the frame_buffer:%d from %d",
						__func__, mCameraId, frame_number, i);
				buf = mResultQ[i].dequeue();
				mRQ.queue(buf);
				num_buffers--;
				ALOGDV("[%s:%d] left buffers:%d", __func__, mCameraId,
						num_buffers);
				if (num_buffers == 0) {
					ALOGDV("[%s:%d] got all:%d buffers", __func__, mCameraId,
							request->num_output_buffers);
					sendResult();
					mNumBuffers = num_buffers;
					break;
				} else
					mNumBuffers = num_buffers;
				}
			}
		}
	}
	return true;
}

}; /* namespace android */
