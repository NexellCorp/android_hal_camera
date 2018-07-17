#define LOG_TAG "NXStream"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>
#include <camera/CameraMetadata.h>

#include <gralloc_priv.h>

#include <nx-scaler.h>
#include "GlobalDef.h"
#include "v4l2.h"
#include "metadata.h"
#include "Stream.h"

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

int Stream::allocBuffer(uint32_t w, uint32_t h, uint32_t format, buffer_handle_t *p)
{
	int ret = NO_ERROR, stride = 0;
	buffer_handle_t ph;

	if (!mAllocator) {
		ALOGE("mAllocator is not exist");
		return -ENODEV;
	}
	ret = mAllocator->alloc(mAllocator, w, h, format,
			GRALLOC_USAGE_HW_CAMERA_WRITE, &ph, &stride);
	if (ret) {
		ALOGE("Failed to alloc a new buffer:%d", ret);
		return -ENOMEM;
	}
	*p = ph;
	return ret;
}

bool Stream::calScalingFactor(const camera_metadata_t *request, uint32_t *crop)
{
	CameraMetadata meta;
	uint32_t active_width = 0, active_height = 0;
	uint32_t calX = 0, calY = 0, calW = 0, calH = 0;
	bool scaling = false;
	uint32_t cropX = 0, cropY = 0, cropWidth = 0, cropHeight = 0;

	meta  = request;
	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		cropX = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		cropY = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		cropWidth = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		cropHeight = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		ALOGDV("CROP: left:%d, top:%d, width:%d, height:%d",
			   cropX, cropY, cropWidth, cropHeight);
	} else {
		meta.clear();
		return scaling;
	}
	meta.clear();

	getActiveArraySize(mCameraId, &active_width, &active_height);
	if (active_width == 0 || active_height == 0)
		return scaling;

	if (((cropWidth - cropX) != active_width) &&
			((cropHeight -cropY) != active_height)) {
		calX = cropX * mStream->width / active_width;
		calW = cropWidth * mStream->width / active_width;
		calY = cropY * mStream->height / active_height;
		calH = cropHeight * mStream->height / active_height;
		/* align 32pixel for cb, cr 16 pixel align */
		calX = (calX + 31) & (~31);
		scaling = true;
		if ((calX + calW) > mStream->width) {
			ALOGDD("[%s] x:%d, w:%d, width:%d", __func__, calX, calW, mStream->width);
			calW = mStream->width - calX;
		}
		if ((calY + calH) > mStream->height) {
			ALOGDD("[%s] y:%d, h:%d, height:%d", __func__, calY, calH, mStream->height);
			calH = mStream->height - calY;
		}
		crop[0] = calX;
		crop[1] = calY;
		crop[2] = calW;
		crop[3] = calH;
		scaling = true;
	}
	return scaling;
}

int Stream::scaling(private_handle_t *dstBuf, private_handle_t *srcBuf,
		const camera_metadata_t *request)
{
	uint32_t crop[4] = {0,};
	bool scaling = false;

	if ((mScaler < 0) || (!dstBuf) || (!srcBuf)) {
		ALOGE("scaler fd:%d or buf(src-%p:dst-%p) is invalid",
				mScaler, srcBuf, dstBuf);
		return -ENODEV;
	}

#ifdef CAMERA_USE_ZOOM
	scaling = calScalingFactor(request, crop);
#else
	(void)(request);
#endif

	ALOGDD("[%s:%d:%d] %s", __func__, mCameraId, mType, (scaling) ? "scaling" : "copy");
	if (!scaling) {
		crop[0] = 0;
		crop[1] = 0;
		crop[2] = srcBuf->width;
		crop[3] = srcBuf->height;
	}
	ALOGDD("[%s:%d:%d] scaling[%d:%d:%d:%d]", __func__, mCameraId, mType,
			crop[0], crop[1], crop[2], crop[3]);

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = getModule();
	android_ycbcr src, dst;
	struct nx_scaler_context ctx;
	int ret = 0;

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

	ctx.crop.x = crop[0];
	ctx.crop.y = crop[1];
	ctx.crop.width = crop[2];
	ctx.crop.height = crop[3];

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
		ALOGE("[%s] Failed to scaler set & run ioctl", __func__);
		goto unlock;
	}
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
	ALOGDD("[%s:%d:%d] scaling done", __func__, mCameraId, mType);
	return ret;
}

int Stream::jpegEncoding(private_handle_t *dst, private_handle_t *src, exif_attribute_t *exif)
{
	android_ycbcr srcY;
	void *dstV;
	int ret;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	if (exif == NULL) {
		ALOGE("[%s] Exif is NULL", __func__);
		return -EINVAL;
	}
	ALOGDD("start jpegEncoding src width:%d, height:%d", src->width, src->height);

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
	ALOGDV("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
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
	ALOGDV("Exif size:%d", exifSize);

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
	ALOGDV("capture success: jpegSize(%d), totalSize(%d)",
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

	ALOGDV("end jpegEncoding");
	return ret;
}

int Stream::skipFrames(void)
{
	NXCamera3Buffer *buf = NULL;
	buffer_handle_t frames[NUM_OF_SKIP_FRAMES];
	private_handle_t *p;
	size_t bufferCount = 0, i = 0;
	int ret = -EINVAL, dma_fd = 0, dqIndex = 0, fd = 0;
	private_handle_t *ph;

	if (!NUM_OF_SKIP_FRAMES)
		return NO_ERROR;

	ALOGDD("[%s:%d:%d]", __func__, mCameraId, mType);

	bufferCount = mQ.size();
	if (bufferCount <= 0) {
		ALOGDV("[%s:%d:%d] mQ.size is invalid", __func__, mCameraId, mType);
		return ret;
	}

	buf = mQ.getHead();
	if (!buf) {
		ALOGE("failed to get buf from queue");
		return ret;
	}

#ifdef CAMERA_USE_ZOOM
	ph = buf->getZoomPrivateHandle();
#else
	if (mScaling)
		ph = buf->getZoomPrivateHandle();
	else
		ph = buf->getPrivateHandle();
#endif
	ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("Failed to setBufferFormat:%d, mFd:%d", ret, mFd);
		goto drain;
	}

	mMaxBufIndex = NUM_OF_SKIP_FRAMES + bufferCount;
	ret = v4l2_req_buf(mFd, mMaxBufIndex);
	if (ret) {
		ALOGE("Failed to req buf : %d, mFd:%d", ret, mFd);
		goto drain;
	}

	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		ret = allocBuffer(ph->width, ph->height, ph->format, &frames[i]);
		if (ret) {
			goto free;
		}
		p = (private_handle_t *)frames[i];
		dma_fd = p->share_fd;
		ret = v4l2_qbuf(mFd, i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("[%d] Failed to v4l2_qbuf (index:%zu), ret:%d",
					mType, i, ret);
			goto fail;
		}
	}

#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
#ifdef CAMERA_SUPPORT_SCALING
	if (mScaling)
#endif
	{
		int w = ph->width;
		int h = ph->height;

#ifndef CAMERA_USE_ZOOM
		getAvaliableResolution(mCameraId, &w, &h);
		if ((w == ph->width) && (h == ph->height)) {
			ALOGE("[%s:%d:%d] Failed to get avaliable stream size",
					__func__, mCameraId, mType);
			ret = -EINVAL;
			goto fail;
		}
#endif
		if (mCrop) {
			w = mCropInfo.width;
			h = mCropInfo.height;
		}
		for (i = 1; i < MAX_BUFFER_COUNT; i++) {
			ret = allocBuffer(w, h, ph->format, &mZmBuf[i]);
			if (mZmBuf[i] == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for scaling",
						__func__, mCameraId, mType);
				ret = -ENOMEM;
				goto fail;
			}
		}
	}
#endif


	for (i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		ALOGDV("[%d] mQ.dequeue: %p", mType, buf);
		if (!buf) {
			ALOGE("Fail - FATAL ERROR: Check Q!!");
			ret = -EINVAL;
			goto fail;
		}
#ifdef CAMERA_USE_ZOOM
		dma_fd = buf->getZoomDmaFd();
#else
	if (mScaling)
		dma_fd = buf->getZoomDmaFd();
	else
		dma_fd = buf->getDmaFd();
#endif
		ret = v4l2_qbuf(mFd, NUM_OF_SKIP_FRAMES+i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("[%d] Failed to v4l2_qbuf for preview(index:%zu), ret:%d",
					mType, i, ret);
			goto fail;
		}
		mRQ.queue(buf);
		ALOGDV("[%d] mRQ.queue: %p", mType, buf);
	}
	setQIndex(bufferCount);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("Failed to stream on:%d", ret);
		goto fail;
	}

	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		ret = v4l2_dqbuf(mFd, &dqIndex, &fd, 1);
		if (ret) {
			ALOGE("Failed to dqbuf for preview:%d", ret);
			goto stop;
		}
		ALOGDV("[%d] dqIndex %d", mType, dqIndex);
	}
	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		if ((mAllocator) && (frames[i]))
			mAllocator->free(mAllocator, frames[i]);
	}
	return NO_ERROR;

stop:
	if (v4l2_streamoff(mFd))
		ALOGE("Failed to stream off");

fail:
	ALOGDV("[%s:%d:%d] fail : %d", __func__, mCameraId, mType, ret);
	if (v4l2_req_buf(mFd, 0))
		ALOGE("Failed to reqbuf");
free:
	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		if ((mAllocator) && (frames[i]))
			mAllocator->free(mAllocator, frames[i]);
	}
#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		if ((mAllocator) && (mZmBuf[i])) {
			mAllocator->free(mAllocator, (buffer_handle_t)mZmBuf[i]);
			mZmBuf[i] = NULL;
		}
	}
#endif

drain:
	ALOGE("[%s] Failed to skip frames:%d", __func__, ret);
	drainBuffer();
	return ret;
}

bool Stream::isThisStream(camera3_stream_t *b)
{
	ALOGDV("[%s:%d:%d] Stream format:0x%x, width:%d, height:%d, usage:0x%x",
			__func__, mCameraId, mType, mStream->format, mStream->width,
			mStream->height, mStream->usage);
	if (b->format == mStream->format) {
		if ((b->width == mStream->width) &&
				(b->height == mStream->height)
				&& (b->usage == mStream->usage))
			return true;
	}
	return false;
}

int Stream::sendResult(void)
{
	int ret = NO_ERROR;

	ALOGDV("[%s:%d:%d]", __func__, mCameraId, mType);

	NXCamera3Buffer *buf = mRQ.getHead();
	if (!buf) {
		ALOGE("[%s] failed to get buffer", __func__);
		return -EINVAL;
	}
	ALOGDV("[%s:%d:%d]frame_number:%d", __func__, mCameraId, mType,
			buf->getFrameNumber());

#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
	if (!mSkip) {
		private_handle_t *ph;
		if ((mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf))
			ph = (private_handle_t *)mTmpBuf;
		else
			ph = buf->getPrivateHandle();
#ifdef CAMERA_SUPPORT_SCALING
		if (mScaling)
#endif
			scaling(ph, buf->getZoomPrivateHandle(), buf->getMetadata());
	}
#endif
	if ((mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf)) {
		exif_attribute_t *exif = new exif_attribute_t();
		uint32_t crop[4] = {0, };
		translateMetadata(mCameraId, buf->getMetadata(), exif, 0, 0);
		if (calScalingFactor(buf->getMetadata(), crop))
			exif->setCropResolution(crop[0], crop[1], crop[2], crop[3]);
		else
			exif->setCropResolution(0, 0, mStream->width, mStream->height);
		jpegEncoding(buf->getPrivateHandle(), (private_handle_t*)mTmpBuf, exif);
	}
	mCb->capture_result(mCb, mType, buf);
	buf = mRQ.dequeue();
	mFQ.queue(buf);
	ALOGDV("[%s:%d:%d] Exit", __func__, mCameraId, mType);
	return ret;
}

void Stream::drainBuffer()
{
	int ret = NO_ERROR;

	ALOGDV("[%d] start draining all RQ buffers", mType);

	while (!mQ.isEmpty())
		mRQ.queue(mQ.dequeue());

	while (!mRQ.isEmpty())
		sendResult();

	ALOGDV("[%d] end draining", mType);
}

void Stream::stopV4l2()
{
	if (mSkip)
		return;
	ALOGDV("[%s:%d:%d] enter", __func__, mCameraId, mType);
	int ret = v4l2_streamoff(mFd);
	if (ret)
		ALOGE("Failed to stop stream:%d", ret);
	ret = v4l2_req_buf(mFd, 0);
	if (ret)
		ALOGE("Failed to req buf:%d", ret);

	mQIndex = 0;
	ALOGDV("[%s:%d:%d] exit", __func__, mCameraId, mType);
}

void Stream::stopStreaming()
{
	ALOGDD("[%s:%d:%d] Enter, mQ:%d, mRQ:%d", __func__, mCameraId, mType,
			mQ.size(), mRQ.size());

	while(!mQ.isEmpty() || !mRQ.isEmpty()) {
		ALOGDV("[%d] Wait Buffer drained", mType);
		usleep(1000);
	}

	stopV4l2();

	if ((mAllocator) && (mTmpBuf))
		mAllocator->free(mAllocator, (buffer_handle_t)mTmpBuf);
	mTmpBuf = NULL;
#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
	for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
		if ((mAllocator) && (mZmBuf[i])) {
			mAllocator->free(mAllocator, (buffer_handle_t)mZmBuf[i]);
			mZmBuf[i] = NULL;
		}
	}
#endif
	ALOGDD("[%s:%d:%d] Exit", __func__, mCameraId, mType);

	if (isRunning()) {
		ALOGDV("[%s:%d:%d] requestExitAndWait Enter", __func__, mCameraId, mType);
		requestExitAndWait();
		ALOGDV("[%s:%d:%d] requestExitAndWait Exit", __func__, mCameraId, mType);
	}
}

int Stream::setBufferFormat(private_handle_t *buf)
{
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;
	int ret, f, width = 0, height = 0;
	uint32_t num_planes = 3;
	uint32_t strides[num_planes];
	uint32_t sizes[num_planes];

	ret = module->lock_ycbcr(module, buf, PROT_READ | PROT_WRITE, 0, 0,
	buf->width, buf->height, &ycbcr);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", buf->share_fd);
		return -EINVAL;
	}

	strides[0] = (uint32_t)ycbcr.ystride;
	strides[1] = strides[2] = (uint32_t)ycbcr.cstride;
	if (buf->format == HAL_PIXEL_FORMAT_YV12) {
		sizes[0] = (uint64_t)(ycbcr.cr) - (uint64_t)(ycbcr.y);
		sizes[1] = sizes[2] = (uint64_t)ycbcr.cb - (uint64_t)ycbcr.cr;
		f = V4L2_PIX_FMT_YVU420;
	} else {
		sizes[0] = (uint64_t)(ycbcr.cb) - (uint64_t)(ycbcr.y);
		sizes[1] = sizes[2] = (uint64_t)ycbcr.cr - (uint64_t)ycbcr.cb;
		f = V4L2_PIX_FMT_YUV420;
	}
	mSize = sizes[0] + sizes[1] + sizes[2];
	if (buf->size < (int)mSize) {
		ALOGE("[%s:%d:%d] invalid size:%d", __func__, mCameraId, mType, buf->size);
		return -EINVAL;
	}

	if (mCrop) {
		getAvaliableResolution(mCameraId, &width, &height);
	} else {
		width = buf->width;
		height = buf->height;
	}
	ret = v4l2_set_format(mFd, f, width, height, num_planes, strides, sizes);
	if (ret) {
		ALOGE("Failed to set format: %d", ret);
		return ret;
	}
	if (mCrop) {
		ret = v4l2_set_crop(mFd, &mCropInfo);
		if (ret) {
			ALOGE("Failed to set crop: %d", ret);
			return ret;
		}
	}
	if (module != NULL) {
		ret = module->unlock(module, buf);
		if (ret) {
			ALOGE("[%s] Failed to gralloc unlock:%d", __func__, ret);
			return ret;
		}
	}
	return 0;
}

int Stream::registerBuffer(uint32_t fNum, const camera3_stream_buffer *buf,
		const camera_metadata_t *meta)
{
	int ret = NO_ERROR;

	NXCamera3Buffer *buffer = mFQ.dequeue();
	if (!buffer) {
		ALOGE("Failed to dequeue NXCamera3Buffer from mFQ");
		return -ENOMEM;
	}
	private_handle_t *b = (private_handle_t*)*buf->buffer;
	ALOGDV("[%s:%d:%d] Enter frame_number:%d, width:%d, height:%d",
			__func__, mCameraId, mType, fNum, b->width, b->height);
#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
#ifdef CAMERA_SUPPORT_SCALING
	if (!mScaling)
		buffer->init(fNum, buf->stream, buf->buffer, meta);
	else
#endif
	{
		if (mZmBuf[0] == NULL) {
			int width = b->width, height = b->height;
#ifndef CAMERA_USE_ZOOM
			getAvaliableResolution(mCameraId, &width, &height);
			if ((width == b->width) && (height == b->height)) {
				ALOGE("[%s:%d:%d] Failed to get avaliable stream size",
						__func__, mCameraId, mType);
				return -EINVAL;
			}
#endif
			if (mCrop) {
				width = mCropInfo.width;
				height = mCropInfo.height;
			}
			allocBuffer(width, height, b->format, &mZmBuf[0]);
			if (mZmBuf[0] == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for scaling",
						__func__, mCameraId, mType);
				return -ENOMEM;
			}
		}
		uint32_t count = getQIndex();
		if (!mZmBuf[count])
			ALOGE("[%s:%d:%d] mZmBuf[%d]:%p is invalid\n", __func__,
					mCameraId, mType, count, mZmBuf[count]);
		private_handle_t *z = (private_handle_t*)mZmBuf[count];
		ALOGDD("[%s:%d:%d] format:0x%x, width:%d, height:%d size:%d",
				__func__, mCameraId, mType, z->format, z->width,
				z->height, z->size);
		buffer->init(fNum, buf->stream, buf->buffer, mZmBuf[count], meta);
	}
#else
	buffer->init(fNum, buf->stream, buf->buffer, meta);
#endif
	ALOGDI("[%s:%d:%d] format:0x%x, width:%d, height:%d size:%d",
			__func__, mCameraId, mType, b->format, b->width,
			b->height, b->size);
	mQ.queue(buffer);
	return ret;
}

status_t Stream::prepareForRun()
{
	NXCamera3Buffer *buf = NULL;
	size_t bufferCount = 0, i = 0;
	int ret = -EINVAL, dma_fd = 0;
	private_handle_t *ph = NULL;
	buffer_handle_t buffer;
	int width, height, format, buf_count = 0;
	
	ALOGDV("[%s:%d:%d]", __func__, mCameraId, mType);

	if ((NUM_OF_SKIP_FRAMES) && (!mSkip))
		return NO_ERROR;

	bufferCount = mQ.size();
	if (bufferCount <= 0) {
		ALOGE("[%s:%d:%d] mQ.size is invalid", __func__, mCameraId, mType);
		goto drain;
	}

	buf = mQ.getHead();
	if (!buf) {
		ALOGE("failed to get buf from queue");
		goto drain;
	}

	if (mSkip) {
		for (i = 0; i < bufferCount; i++) {
			buf = mQ.dequeue();
			if (buf) {
				ALOGDV("[%d] mQ.dequeue: %p", mType, buf);
				mRQ.queue(buf);
				ALOGDV("[%d] mRQ.queue: %p", mType, buf);
			}
		}
		mMaxBufIndex = bufferCount;
		setQIndex(bufferCount);
		return NO_ERROR;
	}

	ph = buf->getPrivateHandle();
	if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
		format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
		width = mStream->width;
		height = mStream->height;
		allocBuffer(width, height, format, &buffer);
		if (buffer == NULL) {
			ALOGE("[%s:%d:%d] Failed to alloc new buffer for JPEG",
					__func__, mCameraId, mType);
			ret = -ENOMEM;
			goto drain;
		}
		mTmpBuf = buffer;
	}
#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
#ifdef CAMERA_SUPPORT_SCALING
	if (mScaling)
#endif
	{
		width = ph->width;
		height = ph->height;

#ifndef CAMERA_USE_ZOOM
		getAvaliableResolution(mCameraId, &width, &height);
		if ((width == ph->width) && (height == ph->height)) {
			ALOGE("[%s:%d:%d] Failed to get avaliable stream size",
					__func__, mCameraId, mType);
			ret = -EINVAL;
			goto fail;
		}
#endif
		if (mCrop) {
			width = mCropInfo.width;
			height = mCropInfo.height;
		}

		for (i = 1; i < MAX_BUFFER_COUNT; i++) {
			allocBuffer(width, height, format, &mZmBuf[i]);
			if (mZmBuf[i] == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for scaling",
						__func__, mCameraId, mType);
				ret = -ENOMEM;
				goto fail;
			}
		}
		ph = buf->getZoomPrivateHandle();
	}
#endif
	if (ph->format == HAL_PIXEL_FORMAT_BLOB)
		ret = setBufferFormat((private_handle_t*)mTmpBuf);
	else
		ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("[%s:%d:%d] failed to setBufferFormat:%d, mFd:%d",
				__func__, mCameraId, mType, ret, mFd);
		goto drain;
	}

	if (ph->format == HAL_PIXEL_FORMAT_BLOB)
		mMaxBufIndex = 1;
	else
		mMaxBufIndex = MAX_BUFFER_COUNT;
	ret = v4l2_req_buf(mFd, mMaxBufIndex);
	if (ret) {
		ALOGE("[%s:%d:%d] failed to req buf : %d, mFd:%d", __func__,
				mCameraId, mType, ret, mFd);
		goto drain;
	}

	for (i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		ALOGDV("[%s:%d:%d] mQ.dequeue: %p", __func__, mCameraId, mType, buf);
		if (!buf) {
			ALOGE("[%s:%d:%d] fail - fatal error: check q!!",
					__func__, mCameraId, mType);
			ret = -EINVAL;
			goto fail;
		}
		if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
			ph = (private_handle_t *)mTmpBuf;
			dma_fd = ph->share_fd;
		} else {
#ifdef CAMERA_USE_ZOOM
		dma_fd = buf->getZoomDmaFd();
#else
		if (mScaling)
			dma_fd = buf->getZoomDmaFd();
		else
			dma_fd = buf->getDmaFd();
#endif
		}
		ret = v4l2_qbuf(mFd, i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("[%s:%d:%d] Failed to v4l2_qbuf for preview(index:%zu), ret:%d",
					__func__, mCameraId, mType, i, ret);
			goto fail;
		}
		mRQ.queue(buf);
		ALOGDV("[%s:%d:%d] mRQ.queue: %p", __func__, mCameraId, mType, buf);
	}
	setQIndex(bufferCount);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("[%s:%d:%d] Failed to stream on:%d", __func__, mCameraId, mType, ret);
		goto fail;
	}
	return NO_ERROR;

fail:
	ALOGDI("[%s:%d:%d] fail : %d", __func__, mCameraId, mType, ret);
	if (v4l2_req_buf(mFd, 0))
		ALOGE("Failed to req buf(line:%d), mFd:%d", __LINE__, mFd);
drain:
	drainBuffer();
#if defined(CAMERA_USE_ZOOM) || defined(CAMERA_SUPPORT_SCALING)
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		if ((mAllocator) && (mZmBuf[i])) {
			mAllocator->free(mAllocator, (buffer_handle_t)mZmBuf[i]);
			mZmBuf[i] = NULL;
		}
	}
#endif
	ALOGE("[%s:%d:%d] drain - Failed to prepare for run:%d", __func__, mCameraId,
			mType, ret);
	return ret;
}

int Stream::dQBuf(int *dqIndex)
{
	int ret = 0, fd = 0;

	if (mSkip) {
		usleep(30000);
		ALOGDV("[%s:%d:%d] skip v4l2 dequeue", __func__, mCameraId, mType);
		return ret;
	}
	ret = v4l2_dqbuf(mFd, dqIndex, &fd, 1);
	if (ret) {
		ALOGE("[%s:%d:%d] Failed to dqbuf:%d", __func__, mCameraId, mType, ret);
	}
	return ret;
}

int Stream::qBuf(NXCamera3Buffer *buf)
{
	int ret = 0, dma_fd = 0;

	if (mSkip) {
		usleep(30000);
		ALOGDV("[%s:%d:%d] skip v4l2 queue", __func__, mCameraId, mType);
		return ret;
	}
	if ((mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf)) {
		private_handle_t *ph = (private_handle_t *)mTmpBuf;
		dma_fd = ph->share_fd;
	} else
#ifdef CAMERA_USE_ZOOM
		dma_fd = buf->getZoomDmaFd();
#else
		if (mScaling)
			dma_fd = buf->getZoomDmaFd();
		else
			dma_fd = buf->getDmaFd();
#endif
	ret = v4l2_qbuf(mFd, mQIndex, &dma_fd, 1, &mSize);
	if (ret) {
		ALOGE("[%s:%d:%d] Failed to qbuf index:%d, mFd:%d, ret:%d",
				__func__, mCameraId, mType, mQIndex, mFd, ret);
	}
	return ret;
}

bool Stream::threadLoop()
{
	int dqIndex = 0, qSize = 0, i;
	int ret = NO_ERROR;

	ALOGDV("[%s:%d:%d] mQ:%zu, mRQ:%zu", __func__, mCameraId,
			mType, mQ.size(), mRQ.size());

	if (mRQ.size() > 0) {
		ret = dQBuf(&dqIndex);
		if (ret) {
			ALOGE("[%s:%d:%d] Failed to dqbuf:%d", __func__,
					mCameraId, mType, ret);
			goto stop;
		}
		ALOGDV("[%d:%d] dqIndex %d", mCameraId, mType, dqIndex);
		ret = sendResult();
		if (ret) {
			ALOGE("[%s:%d:%d] Failed to send result:%d", __func__,
					mCameraId, mType, ret);
			goto stop;
		}
	}

	qSize = mQ.size();
	if (qSize > 0) {
		for (i = 0; i < qSize; i++) {
			NXCamera3Buffer *buf = mQ.dequeue();
			ALOGDV("[%d:%d] mQ.dequeue:%p, mQIndex:%d", mCameraId, mType, buf, mQIndex);
			ret = qBuf(buf);
			if (ret) {
				ALOGE("Failed to qbuf index:%d, mFd:%d, ret:%d",
						mQIndex, mFd, ret);
				goto stop;
			}
			ALOGDV("[%d:%d] qbuf index:%d", mCameraId, mType, mQIndex);
			mRQ.queue(buf);
			setQIndex(mQIndex+1);
		}
	} else {
		ALOGDV("[%d:%d] underflow of input", mCameraId, mType);
		ALOGDV("[%d:%d] InputSize:%zu, QueuedSize:%zu", mCameraId, mType, mQ.size(), mRQ.size());
		if (mQ.size() == 0 && mRQ.size() == 0) {
			ALOGDV("[%d:%d] NO BUFFER --- wait for stopping", mCameraId, mType);
			usleep(10000);
		}
	}

	return true;

stop:
	ALOGDV("[%d] Stream Thread is stopped", mType);
	drainBuffer();
	stopV4l2();
	return false;
}

}; /* namespace android */
