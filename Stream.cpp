#define LOG_TAG "NXStream"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>

#include <gralloc_priv.h>

#include "GlobalDef.h"
#include "NXQueue.h"
#include "v4l2.h"
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
	gralloc_module_t const *module = getModule();
	buffer_handle_t ph;

	if (!mAllocator) {
		ALOGE("mAllocator is not exist");
		return -ENODEV;
	}
	ret = mAllocator->alloc(mAllocator, w, h, format,
			PROT_READ | PROT_WRITE, &ph, &stride);
	if (ret) {
		ALOGE("Failed to alloc a new buffer:%d", ret);
		return -ENOMEM;
	}
	*p = ph;

	return ret;
}

int Stream::skipFrames(void)
{
	NXCamera3Buffer *buf = NULL;
	buffer_handle_t frames[NUM_OF_SKIP_FRAMES];
	private_handle_t *p;
	size_t bufferCount = 0, i = 0;
	int ret = NO_ERROR, dma_fd = 0, dqIndex = 0, fd = 0;

	if (!NUM_OF_SKIP_FRAMES)
		return ret;

	dbg_stream("[%s:%d]", __func__, mType);

	bufferCount = mQ.size();
	if (bufferCount <= 0) {
		dbg_stream("[%s:%d] mQ.size is invalid", __func__, mType);
		return -EINVAL;
	}

	buf = mQ.getHead();
	if (!buf) {
		ALOGE("Failed to get buf from Queue");
		return -EINVAL;
	}

	private_handle_t *ph = buf->getPrivateHandle();
	ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("Failed to setBufferFormat:%d, mFd:%d", ret, mFd);
		goto drain;
	}

	ret = v4l2_req_buf(mFd, NUM_OF_SKIP_FRAMES + bufferCount);
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
			ALOGE("[%d] Failed to v4l2_qbuf for preview(index:%zu), ret:%d",
					mType, i, ret);
			goto fail;
		}
	}


	for (i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		dbg_stream("[%d] mQ.dequeue: %p", mType, buf);
		if (!buf) {
			ALOGE("Fail - FATAL ERROR: Check Q!!");
			ret = -EINVAL;
			goto fail;
		}
		dma_fd = buf->getDmaFd();
		ret = v4l2_qbuf(mFd, NUM_OF_SKIP_FRAMES+i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("[%d] Failed to v4l2_qbuf for preview(index:%zu), ret:%d",
					mType, i, ret);
			goto fail;
		}
		mRQ.queue(buf);
		dbg_stream("[%d] mRQ.queue: %p", mType, buf);
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
		dbg_stream("[%d] dqIndex %d", mType, dqIndex);
	}

	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		if ((mAllocator) && (frames[i]))
			mAllocator->free(mAllocator, frames[i]);
	}
	mSkip = true;

	return NO_ERROR;

stop:
	ret = v4l2_streamoff(mFd);
	if (ret) {
		ALOGE("Failed to stream off:%d", ret);
		goto fail;
	}

fail:
	dbg_stream("[%s:%d] fail : %d", __func__, mType, ret);
	ret = v4l2_req_buf(mFd, 0);
	if (ret)
		ALOGE("Failed to reqbuf:%d", ret);
free:
	for (i = 0; i < NUM_OF_SKIP_FRAMES; i++) {
		if ((mAllocator) && (frames[i]))
			mAllocator->free(mAllocator, frames[i]);
	}

drain:
	ALOGE("[%s] Failed to set buffer format:%d", __func__, ret);
	drainBuffer();
	return ret;
}

bool Stream::isThisStream(camera3_stream_t *b)
{
	dbg_stream("[%s:%d] Stream format:0x%x, width:%d, height:%d, usage:0x%x",
			__func__, mType, mFormat, mWidth, mHeight, mUsage);
	if ((b->format == (int)mFormat) && (b->width == (int)mWidth) &&
			(b->height == (int)mHeight) && (b->usage == mUsage))
		return true;
	return false;
}

int Stream::sendResult(bool drain)
{
	int ret = NO_ERROR;

	dbg_stream("[%s:%d]", __func__, mType);

	NXCamera3Buffer *buf = mRQ.dequeue();
	if (!buf) {
		ALOGE("[%s] failed to get buffer", __func__);
		return -EINVAL;
	}
	mCb->capture_result(mCb, mType, buf);
	mFQ.queue(buf);
	return ret;
}

void Stream::drainBuffer()
{
	int ret = NO_ERROR;

	dbg_stream("[%d] start draining all RQ buffers", mType);

	while (!mQ.isEmpty())
		mRQ.queue(mQ.dequeue());

	while (!mRQ.isEmpty())
		sendResult(true);

	dbg_stream("[%d] end draining", mType);
}

void Stream::stopV4l2()
{
	dbg_stream("[%s:%d] enter", __func__,mType);

	int ret = v4l2_streamoff(mFd);
	if (ret)
		ALOGE("Failed to stop stream:%d", ret);
	ret = v4l2_req_buf(mFd, 0);
	if (ret)
		ALOGE("Failed to req buf:%d", ret);

	mQIndex = 0;
	dbg_stream("[%s:%d] exit", __func__,mType);
}

void Stream::stopStreaming()
{
	dbg_stream("[%s:%d] Enter", __func__,mType);

	while(!mQ.isEmpty() || !mRQ.isEmpty()) {
		dbg_stream("[%d] Wait Buffer drained", mType);
		usleep(1000);
	}

	if (isRunning()) {
		dbg_stream("[%s:%d] requestExitAndWait Enter", __func__, mType);
		requestExitAndWait();
		dbg_stream("[%s:%d] requestExitAndWait Exit", __func__, mType);
	}

	stopV4l2();

	dbg_stream("[%s:%d] Exit", __func__, mType);
}

int Stream::setBufferFormat(private_handle_t *buf)
{
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;
	int ret, f;
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
	if (buf->size != (int)mSize) {
		dbg_stream("[%s:%d] invalid size:%d", __func__, buf->size, mType);
		return -EINVAL;
	}

	ret = v4l2_set_format(mFd, f, buf->width, buf->height,
			num_planes, strides, sizes);
	if (ret) {
		ALOGE("Failed to set format: %d", ret);
		return ret;
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

int Stream::registerBuffer(uint32_t fNum, const camera3_stream_buffer *buf)
{
	int ret = NO_ERROR;

	dbg_stream("[%s:%d] Enter", __func__, mType);
	NXCamera3Buffer *buffer = mFQ.dequeue();
	if (!buffer) {
		ALOGE("Failed to dequeue NXCamera3Buffer from mFQ");
		return -ENOMEM;
	}
	buffer->init(fNum, buf->stream, buf->buffer);
	private_handle_t *b = buffer->getPrivateHandle();
	dbg_stream("[%s:%d] format:0x%x, width:%d, height:%d size:%d",
			__func__, mType, b->format, b->width,
	b->height, b->size);
	mQ.queue(buffer);
	return ret;
}

status_t Stream::readyToRun()
{
	NXCamera3Buffer *buf;
	size_t bufferCount, i;
	int ret = NO_ERROR, dma_fd;

	if (mSkip)
		return ret;

	if ((mType == NX_RECORD_STREAM) && (MAX_VIDEO_HANDLES == 1)) {
		bufferCount = mQ.size();
		for (i = 0; i < bufferCount; i++) {
			buf = mQ.dequeue();
			dbg_stream("[%d] mQ.dequeue: %p", mType, buf);
			if (!buf)
			mRQ.queue(buf);
			dbg_stream("[%d] mRQ.queue: %p", mType, buf);
		}
		setQIndex(bufferCount);
		return ret;
	}

	dbg_stream("[%s:%d]", __func__, mType);

	if (mQ.size() <= 0) {
		dbg_stream("[%s:%d] mQ.size is invalid", __func__, mType);
		return -EINVAL;
	}

	buf = mQ.getHead();
	if (!buf) {
		ALOGE("failed to get buf from queue");
		return -EINVAL;
	}
	private_handle_t *ph = buf->getPrivateHandle();
	ret = setBufferFormat(ph);
	if (ret) {
		ALOGE("failed to setBufferFormat:%d, mFd:%d", ret, mFd);
		goto drain;
	}
	ret = v4l2_req_buf(mFd, MAX_BUFFER_COUNT);
	if (ret) {
		ALOGE("failed to req buf : %d, mFd:%d", ret, mFd);
		goto drain;
	}

	bufferCount = mQ.size();
	for (i = 0; i < bufferCount; i++) {
		buf = mQ.dequeue();
		dbg_stream("[%d] mQ.dequeue: %p", mType, buf);
		if (!buf) {
			ALOGE("fail - fatal error: check q!!");
			ret = -EINVAL;
			goto fail;
		}
		dma_fd = buf->getDmaFd();
		ret = v4l2_qbuf(mFd, i, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("[%d] Failed to v4l2_qbuf for preview(index:%zu), ret:%d",
					mType, i, ret);
			goto fail;
		}
		mRQ.queue(buf);
		dbg_stream("[%d] mRQ.queue: %p", mType, buf);
	}
	setQIndex(bufferCount);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("Failed to stream on:%d", ret);
		goto fail;
	}
	return NO_ERROR;

drain:
	ALOGE("[%s] Failed to set buffer format:%d", __func__, ret);
	drainBuffer();
fail:
	dbg_stream("[%s:%d] fail : %d", __func__, mType, ret);
	ret = v4l2_req_buf(mFd, 0);
	if (ret)
		ALOGE("Failed to req buf(line:%d):%d, mFd:%d", __LINE__, ret, mFd);
	return ret;
}

bool Stream::threadLoop()
{
	int dqIndex, fd, qSize, dma_fd, i;
	int ret = NO_ERROR;

	dbg_stream("[%d] mQ:%zu, mRQ:%zu", mType, mQ.size(), mRQ.size());
	if (mRQ.size() > 0) {
		if ((mType == NX_RECORD_STREAM) && (MAX_VIDEO_HANDLES == 1))
			goto skipDequeue;
		ret = v4l2_dqbuf(mFd, &dqIndex, &fd, 1);
		if (ret) {
			ALOGE("Failed to dqbuf for preview:%d", ret);
			goto stop;
		}
		dbg_stream("[%d] dqIndex %d", mType, dqIndex);
skipDequeue:
		ret = sendResult();
		if (ret) {
			ALOGE("Failed to send result:%d", ret);
			goto stop;
		}
	}

	qSize = mQ.size();
	if (qSize > 0) {
		for (i = 0; i < qSize; i++) {
			NXCamera3Buffer *buf = mQ.dequeue();
			dbg_stream("[%d] mQ.dequeue:%p, mQIndex:%d", mType, buf, mQIndex);
			if ((mType == NX_RECORD_STREAM) && (MAX_VIDEO_HANDLES == 1))
				goto skipDequeue;
			dma_fd = buf->getDmaFd();
			ret = v4l2_qbuf(mFd, mQIndex, &dma_fd, 1, &mSize);
			if (ret) {
				ALOGE("Failed to qbuf index:%d, mFd:%d, ret:%d",
						mQIndex, mFd, ret);
				goto stop;
			}
			dbg_stream("[%d] qbuf index:%d", mType, mQIndex);
skipQueue:
			mRQ.queue(buf);
			setQIndex(mQIndex+1);
			}
	} else {
		dbg_stream("[%d] underflow of input", mType);
		dbg_stream("[%d] InputSize:%zu, QueuedSize:%zu", mType, mQ.size(), mRQ.size());
		if (mQ.size() == 0 && mRQ.size() == 0) {
			dbg_stream("[%d] NO BUFFER --- wait for stopping", mType);
			usleep(10000);
		}
	}

	return true;

stop:
	dbg_stream("[%d] Stream Thread is stopped", mType);
	drainBuffer();
	if ((mType == NX_RECORD_STREAM) && (MAX_VIDEO_HANDLES == 1))
		return false;
	stopV4l2();
	return false;
}

}; /* namespace android */
