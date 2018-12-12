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

#include <nx-deinterlacer.h>
#include <nx-scaler.h>
#include "GlobalDef.h"
#include "v4l2.h"
#include "metadata.h"
#include "Stream.h"

namespace android {

#define CAMERA_USAGE		GRALLOC_USAGE_HW_CAMERA_WRITE
#define DEINTERLACER_USAGE	GRALLOC_USAGE_PRIVATE_0

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

int Stream::allocBuffer(uint32_t w, uint32_t h, uint32_t format, uint32_t usage,
		buffer_handle_t *p)
{
	int ret = NO_ERROR, stride = 0;
	buffer_handle_t ph;

	if (!mAllocator) {
		ALOGE("mAllocator is not exist");
		return -ENODEV;
	}
	ret = mAllocator->alloc(mAllocator, w, h, format, usage, &ph, &stride);
	if (ret) {
		ALOGE("Failed to alloc a new buffer:%d", ret);
		return -ENOMEM;
	}
	*p = ph;
	return ret;
}

void Stream::freeAllBuffers(void)
{
	if (!mAllocator)
		return;

	for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
		if (mScaling) {
			if (mZmBuf[i]) {
				mAllocator->free(mAllocator, (buffer_handle_t)mZmBuf[i]);
				mZmBuf[i] = NULL;
			}
			if (mDeinterBuf[i]) {
				mAllocator->free(mAllocator, (buffer_handle_t)mDeinterBuf[i]);
				mDeinterBuf[i] = NULL;
			}
		}
		if (mTmpBuf[i]) {
			mAllocator->free(mAllocator, (buffer_handle_t)mTmpBuf[i]);
			mTmpBuf[i] = NULL;
		}
	}
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
		if (mInterlaced) {
			cropX = crop[0] = 0;
			cropY = crop[1] = 0;
			cropWidth = crop[2] = mStream->width;
			cropHeight = crop[3] = mStream->height;
			scaling = true;
		}
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

int Stream::deinterlacing(private_handle_t *srcBuf1, private_handle_t *srcBuf2,
		private_handle_t *dstBuf)
{
	if ((mDeinterlacer < 0) || (!dstBuf) || (!srcBuf1) || (!srcBuf2)) {
		ALOGE("mDeinterlacer fd:%d or buf(src1-%p:src2-%p:dst-%p) is invalid",
				mDeinterlacer, srcBuf1, srcBuf2, dstBuf);
		return -ENODEV;
	}

	int ret = 0;
	struct frame_data_info pInfo;
	struct frame_data *pSrcFrame0 = &pInfo.src_bufs[0];
	struct frame_data *pSrcFrame1 = &pInfo.src_bufs[1];
	struct frame_data *pSrcFrame2 = &pInfo.src_bufs[2];
	struct frame_data *pDstFrame = &pInfo.dst_bufs[0];

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = getModule();
	android_ycbcr src1, src2, dst;

	ALOGDD("[%s:%d:%d]", __func__, mCameraId, mType);

	ret = module->lock_ycbcr(module, srcBuf1, PROT_READ | PROT_WRITE, 0, 9,
				 srcBuf1->width, srcBuf1->height, &src1);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", srcBuf1->share_fd);
		goto exit;
	}

	ret = module->lock_ycbcr(module, srcBuf2, PROT_READ | PROT_WRITE, 0, 9,
				 srcBuf2->width, srcBuf2->height, &src2);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", srcBuf2->share_fd);
		goto exit;
	}

	ret = module->lock_ycbcr(module, dstBuf, PROT_READ | PROT_WRITE, 0, 9,
				 dstBuf->width, dstBuf->height, &dst);
	if (ret) {
		ALOGE("Failed to lock_ycbcr for the buf - %d", dstBuf->share_fd);
		goto exit;
	}

	pInfo.command = ACT_DIRECT_FD;
	pInfo.width = dstBuf->width;
	pInfo.height = dstBuf->height;
	pInfo.plane_mode = PLANE3;
	pInfo.dst_idx = 0;

	/* EVEN - ODD - EVEN */
        /* EVEN */
	pSrcFrame0->frame_num = 1;
	pSrcFrame0->plane_num = 1;
	pSrcFrame0->frame_type = FRAME_SRC;
	pSrcFrame0->frame_factor = 1;
	pSrcFrame0->plane3.src_stride[0] = src1.ystride;
	pSrcFrame0->plane3.src_stride[1] = src1.cstride;
	pSrcFrame0->plane3.src_stride[2] = src1.cstride;
	pSrcFrame0->plane3.fds[0] = srcBuf1->share_fd;
	pSrcFrame0->plane3.fds[1] = 0;
        pSrcFrame0->plane3.fds[2] = 0;

	/* ODD */
	pSrcFrame1->frame_num = 0;
	pSrcFrame1->plane_num = 1;
	pSrcFrame1->frame_type = FRAME_SRC;
	pSrcFrame1->frame_factor = 1;
	pSrcFrame1->plane3.src_stride[0] = src1.ystride;
	pSrcFrame1->plane3.src_stride[1] = src1.cstride;
	pSrcFrame1->plane3.src_stride[2] = src1.cstride;
	pSrcFrame1->plane3.fds[0] = srcBuf1->share_fd;
	pSrcFrame1->plane3.fds[1] = 0;
	pSrcFrame1->plane3.fds[2] = 0;

	/* EVEN */
	pSrcFrame2->frame_num = 1;
	pSrcFrame2->plane_num = 1;
	pSrcFrame2->frame_type = FRAME_SRC;
	pSrcFrame2->frame_factor = 1;
	pSrcFrame2->plane3.src_stride[0] = src2.ystride;
	pSrcFrame2->plane3.src_stride[1] = src2.cstride;
	pSrcFrame2->plane3.src_stride[2] = src2.cstride;
	pSrcFrame2->plane3.fds[0] = srcBuf2->share_fd;
	pSrcFrame2->plane3.fds[1] = 0;
	pSrcFrame2->plane3.fds[2] = 0;

	/* set dest */
	pDstFrame->frame_num = 0;
	pDstFrame->plane_num = 1;
	pDstFrame->frame_type = FRAME_DST;
	pDstFrame->frame_factor = 256;
	pDstFrame->plane3.dst_stride[0] = dst.ystride;
	pDstFrame->plane3.dst_stride[1] = dst.cstride;
	pDstFrame->plane3.dst_stride[2] = dst.cstride;

	pDstFrame->plane3.fds[0] = dstBuf->share_fd;
	pDstFrame->plane3.fds[1] = 0;
	pDstFrame->plane3.fds[2] = 0;

	ALOGDD("[%s] src stride[%d:%d], dst stride[%d:%d]", __func__, src1.ystride,
			src1.cstride, dst.ystride, dst.cstride);
	ret = nx_deinter_set_and_run(mDeinterlacer, &pInfo);
	if (ret) {
		ALOGE("[%s] Failed to run deinterlacer:%d", __func__, ret);
		goto unlock;
	}
unlock:
	if (module) {
		ret = module->unlock(module, srcBuf1);
		if (ret)
			ALOGE("[%s] Failed to gralloc unlock for srcBuf1:%d",
			      __func__, ret);
		ret = module->unlock(module, srcBuf2);
		if (ret)
			ALOGE("[%s] Failed to gralloc unlock for srcBuf2:%d",
			      __func__, ret);
		ret = module->unlock(module, dstBuf);
		if (ret)
			ALOGE("[%s] Failed to gralloc unlock for dstBuf:%d",
			      __func__, ret);
	}

exit:
	ALOGDD("[%s:%d:%d] deinterlacing done", __func__, mCameraId, mType);
	return ret;
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
	ALOGDD("[%s:%d:%d] scaling[%d:%d:%d:%d] dst[%d:%d]", __func__, mCameraId, mType,
			crop[0], crop[1], crop[2], crop[3], dstBuf->width, dstBuf->height);

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

	if (mScaling)
		ph = buf->getZoomPrivateHandle();
	else
		ph = buf->getPrivateHandle();

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
		ret = allocBuffer(ph->width, ph->height, ph->format,
				CAMERA_USAGE, &frames[i]);
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

	if (mScaling)
	{
		int w = ph->width;
		int h = ph->height;

#ifdef CAMERA_SUPPORT_SCALING
		getAvaliableResolution(mCameraId, &w, &h);
		if ((w == ph->width) && (h == ph->height))
			ALOGDD("[%s:%d:%d] resolution is the same, just copy",
					__func__, mCameraId, mType);
#endif
		if (mInterlaced) {
			struct frame_data dst_frame;

			for (i = 1; i < MAX_BUFFER_COUNT; i++) {
				allocBuffer(w, h, ph->format,
					CAMERA_USAGE | DEINTERLACER_USAGE,
					&mDeinterBuf[i]);
				if (mDeinterBuf[i] == NULL) {
					ALOGE("[%s:%d:%d] Failed to alloc new buffer for Deinterlacing",
							__func__, mCameraId, mType);
					ret = -ENOMEM;
					goto drain;
				}
			}
		}

		if (mCrop) {
			w = mCropInfo.width;
			h = mCropInfo.height;
		}
		for (i = 1; i < MAX_BUFFER_COUNT; i++) {
			ret = allocBuffer(w, h, ph->format,
					CAMERA_USAGE,
					&mZmBuf[i]);
			if (mZmBuf[i] == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for scaling",
						__func__, mCameraId, mType);
				ret = -ENOMEM;
				goto fail;
			}
		}
	}

	for (i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		ALOGDV("[%d] mQ.dequeue: %p", mType, buf);
		if (!buf) {
			ALOGE("Fail - FATAL ERROR: Check Q!!");
			ret = -EINVAL;
			goto fail;
		}

		if (mScaling)
			dma_fd = buf->getZoomDmaFd();
		else
			dma_fd = buf->getDmaFd();

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
	freeAllBuffers();

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

int Stream::sendResult()
{
	int ret = NO_ERROR;
	NXCamera3Buffer *buf = NULL;

	ALOGDV("[%s:%d:%d]", __func__, mCameraId, mType);

	if (mInterlaced && mScaling) {
		if (mDQ.size() == 1)
			mDQ.dequeue();
		else
			buf = mDQ.getSecond();
	} else
		buf = mRQ.getHead();
	if (!buf) {
		ALOGE("[%s] failed to get buffer", __func__);
		return -EINVAL;
	}
	ALOGDV("[%s:%d:%d]frame_number:%d", __func__, mCameraId, mType,
			buf->getFrameNumber());

	if (!mSkip) {
		private_handle_t *ph;
		int ret = 0, count = 0;

		if ((mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf[0]))
			ph = (private_handle_t *)mTmpBuf[0];
		else
			ph = buf->getPrivateHandle();

		if (mScaling)
		{
			//test(ph, buf->getDeinterPrivateHandle());
			if (mInterlaced)
				scaling(ph, buf->getDeinterPrivateHandle(), buf->getMetadata());
			else
				scaling(ph, buf->getZoomPrivateHandle(), buf->getMetadata());
		}
	}

	if (!mSkip && (mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf[mQIndex])) {
		exif_attribute_t *exif = new exif_attribute_t();
		uint32_t crop[4] = {0, };
		translateMetadata(mCameraId, buf->getMetadata(), exif, 0, 0);
		if (calScalingFactor(buf->getMetadata(), crop))
			exif->setCropResolution(crop[0], crop[1], crop[2], crop[3]);
		else
			exif->setCropResolution(0, 0, mStream->width, mStream->height);
		jpegEncoding(buf->getPrivateHandle(), (private_handle_t*)mTmpBuf[mQIndex], exif);
	}
	mCb->capture_result(mCb, mType, buf);
	if (mInterlaced && mScaling)
		buf = mDQ.dequeue();
	else
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

	if (mInterlaced && mScaling) {
		while (!mRQ.isEmpty())
			mDQ.queue(mRQ.dequeue());
		while (!mDQ.isEmpty())
			sendResult();
	} else {
		while (!mRQ.isEmpty())
			sendResult();
	}
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
	ALOGDD("[%s:%d:%d] Enter, mQ:%d, mRQ:%d, mDQ:%d", __func__, mCameraId, mType,
			mQ.size(), mRQ.size(), mDQ.size());

	if (isRunning()) {
		ALOGDV("[%s:%d:%d] requestExitAndWait Enter", __func__, mCameraId, mType);
		requestExitAndWait();
		ALOGDV("[%s:%d:%d] requestExitAndWait Exit", __func__, mCameraId, mType);
	}

	if (!mQ.isEmpty() || !mRQ.isEmpty() || !mDQ.isEmpty()) {
		ALOGDV("[%d] Wait Buffer drained", mType);
		drainBuffer();
		stopV4l2();
	}

	freeAllBuffers();

	ALOGDD("[%s:%d:%d] Exit", __func__, mCameraId, mType);
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
		ALOGDD("[%s:%d] call set crop", __func__, mType);
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
	if (!mScaling)
		buffer->init(fNum, buf->stream, buf->buffer, meta);
	else
	{
		int width = b->width, height = b->height;
		int format = b->format;

		if (b->format == HAL_PIXEL_FORMAT_BLOB)
			format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
#ifdef CAMERA_SUPPORT_SCALING
		getAvaliableResolution(mCameraId, &width, &height);
		if ((width == b->width) && (height == b->height))
			ALOGDD("[%s:%d:%d] resolution is the same, just copy",
				__func__, mCameraId, mType);
#endif
		if (mInterlaced && (mDeinterBuf[0] == NULL)) {
			allocBuffer(width, height, format,
				CAMERA_USAGE | DEINTERLACER_USAGE,
				&mDeinterBuf[0]);
			allocBuffer(width, height, format,
				CAMERA_USAGE,
				&mTmpBuf[0]);
			if ((mDeinterBuf[0] == NULL) || (mTmpBuf[0] == NULL)) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for Deinterlacing",
						__func__, mCameraId, mType);
				return -ENOMEM;
			}
			NXCamera3Buffer *bTmp = mFQ.dequeue();
			if (!bTmp) {
				ALOGE("Failed to dequeue NXCamera3Buffer from mFQ");
				return -ENOMEM;
			}
			bTmp->init(fNum, buf->stream, buf->buffer, mTmpBuf[0], mDeinterBuf[0], meta);
			mQ.queue(bTmp);
		}

		if (mZmBuf[0] == NULL) {
			if (mCrop) {
				width = mCropInfo.width;
				height = mCropInfo.height;
			}
			allocBuffer(width, height, format,
				CAMERA_USAGE,
				&mZmBuf[0]);
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
		if (mInterlaced)
			buffer->init(fNum, buf->stream, buf->buffer, mZmBuf[count], mDeinterBuf[count], meta);
		else
			buffer->init(fNum, buf->stream, buf->buffer, mZmBuf[count], meta);
	}
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

	ALOGD("[%s:%d] bufferCount:%d", __func__, mType, bufferCount);
	if (mSkip) {
		for (i = 0; i < bufferCount; i++) {
			buf = mQ.dequeue();
			if (buf) {
				ALOGDV("[%d] mQ.dequeue: %p", mType, buf);
				mRQ.queue(buf);
				ALOGDV("[%d] mRQ.queue: %p", mType, buf);
			}
		}
		if (mInterlaced) {
			mInterlaced = false;
			if (mRQ.size() > 0) {
				mRQ.dequeue();
				ALOGDV("[%d] mRQ.dequeue: %p", mType, buf);
				bufferCount -= 1;
			}
		}
		mMaxBufIndex = bufferCount;
		setQIndex(bufferCount);
		return NO_ERROR;
	}

	ph = buf->getPrivateHandle();
	format = ph->format;

	if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
		mMaxBufIndex = 1 + mInterlaced;
		format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
	} else
		mMaxBufIndex = MAX_BUFFER_COUNT;

	ALOGDD("[%s:%d] mMaxBufIndex:%d", __func__, mType, mMaxBufIndex);
	if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
		width = mStream->width;
		height = mStream->height;
		for (i = mInterlaced; i < mMaxBufIndex; i++) {
			allocBuffer(width, height, format,
				CAMERA_USAGE,
				&buffer);
			if (buffer == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for JPEG",
						__func__, mCameraId, mType);
				ret = -ENOMEM;
				goto drain;
			}
			mTmpBuf[i] = buffer;
		}
		ph = (private_handle_t *)mTmpBuf[0];
	}

	if (mScaling) {
		width = ph->width;
		height = ph->height;

#ifdef CAMERA_SUPPORT_SCALING
		getAvaliableResolution(mCameraId, &width, &height);
		if ((width == ph->width) && (height == ph->height))
			ALOGDD("[%s:%d:%d] resolution is the same, just copy",
					__func__, mCameraId, mType);
#endif
		if (mInterlaced) {
			for (i = 1; i < mMaxBufIndex; i++) {
				allocBuffer(width, height, format,
					CAMERA_USAGE | DEINTERLACER_USAGE,
					&mDeinterBuf[i]);
				if (mDeinterBuf[i] == NULL) {
					ALOGE("[%s:%d:%d] Failed to alloc new buffer for Deinterlacing",
							__func__, mCameraId, mType);
					ret = -ENOMEM;
					goto drain;
				}
			}
		}

		if (mCrop) {
			width = mCropInfo.width;
			height = mCropInfo.height;
		}

		for (i = 1; i < mMaxBufIndex; i++) {
			allocBuffer(width, height, format,
				CAMERA_USAGE,
				&mZmBuf[i]);
			if (mZmBuf[i] == NULL) {
				ALOGE("[%s:%d:%d] Failed to alloc new buffer for scaling",
						__func__, mCameraId, mType);
				ret = -ENOMEM;
				goto fail;
			}
		}
		ph = buf->getZoomPrivateHandle();
	}

	if (ph->format == HAL_PIXEL_FORMAT_BLOB)
		ret = setBufferFormat((private_handle_t*)mTmpBuf[0]);
	else
		ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("[%s:%d:%d] failed to setBufferFormat:%d, mFd:%d",
				__func__, mCameraId, mType, ret, mFd);
		goto drain;
	}

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

		if (mScaling)
			dma_fd = buf->getZoomDmaFd();
		else {
			if (ph->format == HAL_PIXEL_FORMAT_BLOB) {
				ph = (private_handle_t *)mTmpBuf[i];
				dma_fd = ph->share_fd;
			} else
				dma_fd = buf->getDmaFd();
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
	freeAllBuffers();

	ALOGE("[%s:%d:%d] drain - Failed to prepare for run:%d", __func__, mCameraId,
			mType, ret);
	return ret;
}

int Stream::dQBuf(int *dqIndex)
{
	int ret = 0, fd = 0;

	if (mSkip) {
		usleep(30000);
		dqIndex = 0;
		ALOGDV("[%s:%d:%d] skip v4l2 dequeue", __func__, mCameraId, mType);
		return ret;
	}
	ret = v4l2_dqbuf(mFd, dqIndex, &fd, 1);
	if (ret) {
		ALOGE("[%s:%d:%d] Failed to dqbuf:%d", __func__, mCameraId,
				mType, ret);
		return ret;
	}
	ALOGDD("[%s:%d:%d] dqIndex:%d, dma_fd:%d\n", __func__,
			mCameraId, mType, *dqIndex, fd);

	if (mInterlaced && mScaling) {
		int count = 0, index = *dqIndex;
		struct frame_data frame;
		NXCamera3Buffer *buf = mRQ.getHead();

		if (!buf) {
			ALOGE("[%s] failed to get buffer", __func__);
			return -EINVAL;
		}

		mDQ.queue(mRQ.dequeue());
		if (mDQ.size() > 1) {
			NXCamera3Buffer *src1, *src2;

			src1 = mDQ.getHead();
			if (!src1) {
				ALOGE("[%s] failed to get buffer", __func__);
				return -EINVAL;
			}
			src2 = mDQ.getSecond();
			if (!src2) {
				ALOGE("[%s] failed to get buffer", __func__);
				return -EINVAL;
			}
			ret = deinterlacing(src2->getZoomPrivateHandle(),
					src1->getZoomPrivateHandle(),
					src2->getDeinterPrivateHandle());
			if (ret) {
				ALOGE("[%s] failed to deinterlace:%d", __func__, ret);
				return ret;
			}
		} else {
			*dqIndex = -1;
			return 0;
		}
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
	if (mScaling)
		dma_fd = buf->getZoomDmaFd();
	else {
		if ((mStream->format == HAL_PIXEL_FORMAT_BLOB) && (mTmpBuf[mQIndex])) {
			private_handle_t *ph = (private_handle_t *)mTmpBuf[mQIndex];
			dma_fd = ph->share_fd;
		} else
			dma_fd = buf->getDmaFd();
	}
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
		if (dqIndex >= 0) {
			ret = sendResult();
			if (ret) {
				ALOGE("[%s:%d:%d] Failed to send result:%d", __func__,
						mCameraId, mType, ret);
				goto stop;
			}
		 } else
			 return true;

	}

	if(exitPending())
		goto stop;

	qSize = mQ.size();
	if (qSize > 0) {
		NXCamera3Buffer *buf = mQ.getHead();
		ALOGDV("[%d:%d] mQ.dequeue:%p, mQIndex:%d", mCameraId, mType, buf, mQIndex);
		ret = qBuf(buf);
		if (ret) {
			ALOGE("Failed to qbuf index:%d, mFd:%d, ret:%d",
					mQIndex, mFd, ret);
			goto stop;
		}
		ALOGDV("[%d:%d] qbuf index:%d", mCameraId, mType, mQIndex);
		mRQ.queue(mQ.dequeue());
		setQIndex(mQIndex+1);
	} else {
		ALOGDV("[%d:%d] underflow of input", mCameraId, mType);
		ALOGDV("[%d:%d] InputSize:%zu, QueuedSize:%zu, DequeuedSize:%d",
				mCameraId, mType, mQ.size(), mRQ.size(), mDQ.size());
		if (mQ.size() == 0 && mRQ.size() == 0) {
			ALOGDV("[%d:%d] NO BUFFER --- wait for stopping", mCameraId, mType);
			usleep(10000);
		}
	}

	if(exitPending())
		goto stop;

	return true;

stop:
	ALOGDV("[%d] Stream Thread is stopped", mType);
	drainBuffer();
	stopV4l2();
	return false;
}

}; /* namespace android */
