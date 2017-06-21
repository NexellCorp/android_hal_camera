/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Camera3BufferControl"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <linux/videodev2.h>

#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <utils/Mutex.h>
#include <sync/sync.h>

#include <sys/mman.h>
#include <gralloc_priv.h>
#include <hardware/gralloc.h>

#include <camera/CameraMetadata.h>
#include <hardware/camera.h>
#include <hardware/camera3.h>

#include "Camera3HWInterface.h"
#include "Camera3BufferControl.h"

namespace android {

/*****************************************************************************/
/* V4L2 Interface						     */
/*****************************************************************************/
static int v4l2_set_format(int fd, uint32_t w, uint32_t h, uint32_t num_planes,
			   uint32_t strides[], uint32_t sizes[])
{
	struct v4l2_format v4l2_fmt;

	ALOGD("[%s]\n", __FUNCTION__);

	bzero(&v4l2_fmt, sizeof(struct v4l2_format));

	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	v4l2_fmt.fmt.pix_mp.width = w;
	v4l2_fmt.fmt.pix_mp.height = h;
	v4l2_fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420;
	v4l2_fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	v4l2_fmt.fmt.pix_mp.num_planes = num_planes;
	for (uint32_t i = 0; i < num_planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		plane_fmt = &v4l2_fmt.fmt.pix_mp.plane_fmt[i];
		plane_fmt->sizeimage = sizes[i];
		plane_fmt->bytesperline = strides[i];
		ALOGD("[%d] strides=%d, size=%d\n", i, strides[i], sizes[i]);
	}

	return ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
}

static int v4l2_req_buf(int fd, int count)
{
	struct v4l2_requestbuffers req;

	ALOGD("[%s] req buf count is %d\n", __FUNCTION__, count);

	bzero(&req, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_DMABUF;
	req.count = count;

	return ioctl(fd, VIDIOC_REQBUFS, &req);
}

static int v4l2_qbuf(int fd, uint32_t index, int dma_fds[], uint32_t num_planes,
		     uint32_t sizes[])
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[num_planes];

	ALOGV("[%s]\n", __FUNCTION__);

	bzero(&buf, sizeof(struct v4l2_buffer));
	bzero(planes, sizeof(struct v4l2_plane)*num_planes);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.index = index;

	for (uint32_t i = 0; i < num_planes; i++) {
		planes[i].m.fd = dma_fds[i];
		planes[i].length = sizes[i];
		planes[i].bytesused = planes[i].length;
	}
	buf.length = num_planes;
	buf.m.planes = planes;

	return ioctl(fd, VIDIOC_QBUF, &buf);
}

static int v4l2_dqbuf(int fd, int *index, int32_t dma_fd[], uint32_t num_planes)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[num_planes];
	int ret;

	ALOGV("[%s]\n", __FUNCTION__);

	bzero(&buf, sizeof(struct v4l2_buffer));
	bzero(planes, sizeof(struct v4l2_plane)*num_planes);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.length = num_planes;
	buf.m.planes = planes;

	ret = ioctl(fd, VIDIOC_DQBUF, &buf);
	if (!ret) {
		*index = buf.index;
		for(uint32_t i = 0; i < num_planes; i++)
			dma_fd[i] = planes[i].m.fd;
	}

	return ret;
}

static int v4l2_streamon(int fd)
{
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGD("[%s]\n", __FUNCTION__);

	return ioctl(fd, VIDIOC_STREAMON, &buf_type);
}

static int v4l2_streamoff(int fd)
{
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGD("[%s]\n", __FUNCTION__);
	return ioctl(fd, VIDIOC_STREAMOFF, &buf_type);
}

/*****************************************************************************/
/* Camera3 Buffer Control Class						     */
/*****************************************************************************/
Camera3BufferControl::Camera3BufferControl(int handle,
					 const camera3_callback_ops_t *callback)
{
	mFd = handle;
	mCallback_ops = callback;
	mStreaming = IDLE_MODE;
	mQIndex = 0;
	mDqIndex = 0;
	mReqIndex = 0;
	mCapIndex = 0;

	for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
		memset(&mRequests[i], 0x0, sizeof(capture_result_t));
		memset(&mRequests[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t)*MAX_PROCESSED_STREAMS);
		memset(&mQueued[i], 0x0, sizeof(capture_result_t));
		memset(&mQueued[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t)*MAX_PROCESSED_STREAMS);
	}
}

Camera3BufferControl::~Camera3BufferControl(void)
{
	ALOGD("[%s] destroyed\n", __FUNCTION__);
}

int Camera3BufferControl::setBufferFormat(private_handle_t *buf)
{
	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr;

	ALOGV("[%s] fd:%d, ion_hnd:%p, size:%d, width:%d, height:%d, stride:%d\n",
	      __FUNCTION__, buf->share_fd, buf->ion_hnd, buf->size,
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

	ALOGV("[%s] ystride:%zu, cstride:%zu\n", __FUNCTION__, ycbcr.ystride,
	      ycbcr.cstride);

	strides[0] = (uint32_t)ycbcr.ystride;
	sizes[0] = (uint64_t)(ycbcr.cb) - (uint64_t)(ycbcr.y);
	strides[1] = strides[2] = (uint32_t)ycbcr.cstride;
	sizes[1] = sizes[2] = (uint64_t)ycbcr.cr - (uint64_t)ycbcr.cb;

	mSize = 0;
	for (uint32_t i = 0; i < num_planes; i++) {
		ALOGV("[%d] mstrides = %d, sizes = %d\n",
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

void Camera3BufferControl::setStreamingMode(uint32_t mode)
{
	ALOGD("[%s] streaming mode is %d to %d\n", __FUNCTION__,
	      mStreaming, mode);
	mStreaming = mode;
}

uint32_t Camera3BufferControl::getStreamingMode(void)
{
	return mStreaming;
}

int Camera3BufferControl::getQIndex(void)
{
	return mQIndex;
}

int Camera3BufferControl::sendNotify(bool error, uint32_t frame_number,
				     nsecs_t timestamp)
{
	camera3_notify_msg_t msg;

	ALOGV("[%s] %s - frame_number : %d, timestamp : %ld\n", __FUNCTION__,
	      (error) ? "Error" : "Success", frame_number, timestamp);

	memset(&msg, 0x0, sizeof(camera3_notify_msg_t));

	if (error) {
		msg.type = CAMERA3_MSG_ERROR;
		msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
		msg.message.error.error_stream = NULL;
		msg.message.error.frame_number = 0;
	} else {
		msg.type = CAMERA3_MSG_SHUTTER;
		msg.message.shutter.frame_number = frame_number;
		msg.message.shutter.timestamp = timestamp;
	}
	mCallback_ops->notify(mCallback_ops, &msg);

	return 0;
}

int Camera3BufferControl::sendCaptureResult(bool queued, int index)
{
	camera3_capture_result_t result;
	capture_result_t buf;

	mMutex.lock();
	if (queued)
		buf = mQueued[index];
	else
		buf = mRequests[index];
	mMutex.unlock();

	ALOGV("[%s] index:%d\n", __FUNCTION__, index);

	bzero(&result, sizeof(camera3_capture_result_t));
	for (uint32_t i = 0; i < buf.num_output_buffers; i++) {
		buf.output_buffers[i].release_fence  = -1;
		buf.output_buffers[i].acquire_fence  = -1;
	}
	result.frame_number = buf.frame_number;

		result.result = NULL;

	result.num_output_buffers = buf.num_output_buffers;
	result.output_buffers = buf.output_buffers;
	if (result.result == NULL)
		result.partial_result = 0;
	else
		result.partial_result = 1;
	result.input_buffer = NULL;

	ALOGV("[%s] frame_number:%d, num_buffers:%d, buffers:%p, status:%d\n",
	      __FUNCTION__, result.frame_number, result.num_output_buffers,
	      result.output_buffers, result.output_buffers->status);

	mCallback_ops->process_capture_result(mCallback_ops, &result);

	return 0;
}

int Camera3BufferControl::initStreaming(private_handle_t *buffer)
{
	int ret;
	status_t res;

	ALOGD("[%s]\n", __FUNCTION__);

	ret = setBufferFormat(buffer);
	if (ret) {
		ALOGE("failed to set Buffer Format:%d\n", ret);
		return ret;
	}

	ret = v4l2_req_buf(mFd, MAX_BUFFER_COUNT);
	if (ret) {
		ALOGE(" failed to req buf : %d\n", ret);
		return ret;
	}
	return 0;
}

int Camera3BufferControl::stopStreaming(void)
{
	int ret;

	ALOGD("[%s]\n", __FUNCTION__);

	if (!mStreaming)
		return 0;

	mStreaming = STOP_MODE;

	ret = v4l2_streamoff(mFd);
	if (ret) {
		ALOGE("failed to stream on:%d\n", ret);
		return -EINVAL;
	}

	ret = v4l2_req_buf(mFd, 0);
	if (ret) {
		ALOGE(" failed to req buf : %d\n", ret);
		return -EINVAL;
	}
	mStreaming = IDLE_MODE;

	return 0;
}

int Camera3BufferControl::removeBuffer(int index, bool queued)
{
	int i, bufCount;
	capture_result_t *buf;

	mMutex.lock();

	if (queued) {
		buf = mQueued;
		bufCount = mQIndex;
	} else {
		buf = mRequests;
		bufCount = mReqIndex;
	}
	ALOGV("[%s] queued:%d bufcount:%d\n", __FUNCTION__, queued, bufCount);

	for (i = 0; i < bufCount; i++) {
		if (i == index) {
			memset(&buf[i], 0x0, sizeof(capture_result_t));
			memset(buf[i].output_buffers, 0x0,
			       sizeof(camera3_stream_buffer_t)*MAX_PROCESSED_STREAMS);
			for (; i < (bufCount-1); i++) {
				memcpy(&buf[i], &buf[i+1],
				       sizeof(capture_result_t));
				memcpy(buf[i].output_buffers,
				       buf[i+1].output_buffers,
				       sizeof(camera3_stream_buffer_t)*MAX_PROCESSED_STREAMS);
			}
			bufCount--;
		}
	}
	if (queued)
		mQIndex = bufCount;
	else
		mReqIndex = bufCount;
	ALOGV("[%s] End QIndex:%d, ReqIndex:%d, i:%d\n", __FUNCTION__, mQIndex,
	      mReqIndex, i);
	mMutex.unlock();

	return 0;
}

int Camera3BufferControl::getBuffer(int fd)
{
	uint32_t i;
	const camera3_stream_buffer_t *output;

	ALOGV("[%s] fd:%d\n", __FUNCTION__, fd);

	mMutex.lock();
	for (i = 0; i < mQIndex; i++) {
		if (mQueued[i].fd == fd) {
			ALOGV("[%s] i is %d\n", __FUNCTION__, i);
			mMutex.unlock();
			return i;
		}
	}
	ALOGV("[%s] i:%d\n", __FUNCTION__, i);
	mMutex.unlock();
	return -ENOMEM;
}

int Camera3BufferControl::addBuffer(capture_result_t *buf, bool queued,
				    uint32_t qIndex)
{
	int ret = 0, index = 0;

	mMutex.lock();
	if (queued)
		index = mQIndex;
	else
		index = mReqIndex;
	mMutex.unlock();

	ALOGV("[%s] Queued:%d, Index: %d\n", __FUNCTION__, queued, index);

	if ((index >= MAX_BUFFER_COUNT) ||(index < 0)) {
		ALOGE("buffer is already full queued\n");
		return -EINVAL;
	}

	if (queued) {
		int dma_fd = buf->fd;
		ret = v4l2_qbuf(mFd, qIndex, &dma_fd, 1, &mSize);
		if (ret) {
			ALOGE("failed to qbuf-result:%d, index:%d\n",
			ret, index);
			return ret;
		}
		mMutex.lock();
		memcpy(&mQueued[mQIndex], buf, sizeof(capture_result_t));
		memcpy(mQueued[mQIndex].output_buffers, buf->output_buffers,
		       sizeof(camera3_stream_buffer_t)*buf->num_output_buffers);
		mQIndex++;
		ALOGV("[%s] mQIndex: %d\n", __FUNCTION__, mQIndex);
		mMutex.unlock();
	} else {
		mMutex.lock();
		memcpy(&mRequests[mReqIndex], buf, sizeof(capture_result_t));
		memcpy(mRequests[mReqIndex].output_buffers,
		       buf->output_buffers,
		       sizeof(camera3_stream_buffer_t)*buf->num_output_buffers);
		mReqIndex++;
		ALOGV("[%s] mReqIndex: %d\n", __FUNCTION__, mReqIndex);
		mMutex.unlock();
	}

	return 0;
}

int Camera3BufferControl::moveToQueued(uint32_t index)
{
	int ret = 0;

	ALOGV("[%s] mReqIndex: %d\n", __FUNCTION__, mReqIndex);

	ret = addBuffer(&mRequests[0], true, index);
	removeBuffer(0, false);
	return ret;
}

int Camera3BufferControl::registerBuffer(camera3_capture_request_t *request,
					 bool capture)
{
	capture_result_t buffer;

	mMutex.lock();
	if ((mQIndex >= MAX_BUFFER_COUNT) && (mReqIndex >= MAX_BUFFER_COUNT)) {
		ALOGE("Max Buffer is already queued : %d, %d\n",
		      mQIndex, mReqIndex);
		mMutex.unlock();
		return -EINVAL;
	}
	mMutex.unlock();

	bzero(&buffer, sizeof(capture_result_t));
	ALOGV("[%s] frame_number:%d, num_buffers:%d\n",
	      __FUNCTION__, request->frame_number, request->num_output_buffers);
	for (uint32_t i = 0; i < request->num_output_buffers; i++) {
		const camera3_stream_buffer_t *output =
			&request->output_buffers[i];
		private_handle_t *buf =
			(private_handle_t *)*output->buffer;

		ALOGV("[%d] streamType:%d, format:0x%x, width:%d, height:%d,usage:0x%x\n",
		      i, output->stream->stream_type, output->stream->format,
		      output->stream->width, output->stream->height,
		      output->stream->usage);

		if (output->status) {
			ALOGE("[%s] Buffer is not valid to use:%d\n",
			      __FUNCTION__, output->status);
			sendNotify(true, 0, 0);
			return -EINVAL;
		}

		if (output->release_fence != -1) {
			ALOGE("[%s] Bad Value, release fence is not -1\n",
			      __FUNCTION__);
			return -EINVAL;
		}

		if (output->acquire_fence != -1) {
			ALOGV("[%s] acquire fence is exist:%d\n", __FUNCTION__,
			      output->acquire_fence);
			int32_t rc = sync_wait(output->acquire_fence, -1);
			ALOGV("[%s] fence is released\n", __FUNCTION__);
			close(output->acquire_fence);
			if (rc != OK) {
				ALOGE("[%s] sync wait is failed : %d\n",
				      __FUNCTION__, rc);
				return rc;
			}
		}

		if (buf->share_fd < 0) {
			ALOGE("[%s] Invalid fd is received:%d\n", __FUNCTION__,
			      buf->share_fd);
			return -EINVAL;
		}
		buffer.capture = capture;
		if (request->num_output_buffers > 1) {
			if (buf->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
				buffer.fd = buf->share_fd;
				memcpy(&buffer.output_buffers[0], output,
				       sizeof(camera3_stream_buffer_t));
			} else {
				memcpy(&buffer.output_buffers[1], output,
				       sizeof(camera3_stream_buffer_t));
			}
			ALOGV("[%d] format = 0x%x, fd :%d, width:%d, height:%d\n",
			      i, buf->format, buffer.fd, buf->width, buf->height);
		} else {
			buffer.fd = buf->share_fd;
			memcpy(&buffer.output_buffers[i], output,
			       sizeof(camera3_stream_buffer_t));
		}
		buffer.frame_number = request->frame_number;
		buffer.num_output_buffers =
			request->num_output_buffers;
		buffer.timestamp =
			(systemTime(SYSTEM_TIME_MONOTONIC)/
			 default_frame_duration);
		ALOGV("[%s] fd:%d, capture:%d, frameNumber:%d, num_buffers:%d, timestamp:%lld\n",
		      __FUNCTION__,
		      buffer.fd, buffer.capture, buffer.frame_number,
		      buffer.num_output_buffers,
		      buffer.timestamp);
	}
	mMutex.lock();
	int index = (mStreaming) ? mDqIndex: mQIndex;
	bool queued = (!mStreaming);
	mMutex.unlock();
	addBuffer(&buffer, queued, index);
	sendNotify(false, buffer.frame_number, buffer.timestamp);

	return 0;
}

int Camera3BufferControl::checkResult(int index)
{
	void *virt;
	const camera3_stream_buffer_t *output =
		&mQueued[index].output_buffers[0];
	private_handle_t *buf =
		(private_handle_t*)*output->buffer;

	virt = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    mQueued[index].fd, 0);
	if (virt == MAP_FAILED) {
		ALOGE("[%s] Failed to get virtual addr\n", __FUNCTION__);
		return -1;
	}
	ALOGE("[%s] %p\n", __FUNCTION__, virt);

	char *data = (char*)virt;
	for (int i = 0; i < 4; i++) {
		ALOGE("[0x%x]\n", *data);
		data++;
	}
	memset(virt, 0x10, buf->size);
	ALOGV("[%s] hnd:%p, fd:%d, size:%d, stride:%d\n", __FUNCTION__,
	      buf->ion_hnd, buf->share_fd, buf->size, buf->stride);
	/*
	memset(buf + (mStrides[0] * mHeight), 0x80, mSizes[1]);
	memset(buf + ((mStrides[0] * mHeight) + (mStrides[1] * (mHeight /2))),
	       0x80, mSizes[2]);
	*/
	return 0;
}

int Camera3BufferControl::flush() {
	int ret, dma_fd;
	uint32_t i, reqIndex, qIndex;

	ALOGD("[%s]\n", __FUNCTION__);

	if (mStreaming == STREAMING_MODE) {
		ret = stopStreaming();
		if (ret)
			return -EINVAL;
	}

	mMutex.lock();
	reqIndex = mReqIndex;
	qIndex = mQIndex;
	mMutex.unlock();

#if 0
	camera3_capture_result_t result;
	camera3_stream_buffer_t buf[qIndex];

	for (i = 0; i < qIndex + reqIndex; i++) {
		buf[i].release_fence  = -1;
		buf[i].acquire_fence  = -1;
		buf[i].status = CAMERA3_BUFFER_STATUS_ERROR;
	}
	memset(&result, 0x0, sizeof(camera3_capture_result_t));
	result.result = NULL;
	result.num_output_buffers = qIndex+reqIndex;
	result.output_buffers = buf;
	result.partial_result = 0;
	result.input_buffer = NULL;
	result.frame_number = mQueued[0].frame_number;
	mCallback_ops->process_capture_result(mCallback_ops, &result);
#else
	for (i = 0; i < qIndex; i++)
		sendCaptureResult(true, i);
	for (i = 0; i < reqIndex; i++)
		sendCaptureResult(false, i);
#endif
	mMutex.lock();
	/*
	for (i = 0; i < MAX_BUFFER_COUNT; i++) {
		memset(&mRequests[i], 0x0, sizeof(capture_result_t));
		memset(&mRequests[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t));
		memset(&mQueued[i], 0x0, sizeof(capture_result_t));
		memset(&mQueued[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t));
	}
	*/
	mQIndex = 0;
	mReqIndex = 0;
	mMutex.unlock();

	ALOGD("[%s] Finished\n", __FUNCTION__);

	return 0;
}

status_t Camera3BufferControl::readyToRun(void)
{

	int ret, dma_fd;

	ALOGD("[%s]\n", __FUNCTION__);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("failed to stream on:%d\n", ret);
		return ret;
	}
	mStreaming = STREAMING_MODE;

	return NO_ERROR;
}

int Camera3BufferControl::releaseVirtForHandle(private_handle_t const *handle,
					       unsigned long virt)
{
	munmap((void*)virt, handle->size);
	return 0;
}

int Camera3BufferControl::getVirtForHandle(private_handle_t const *handle,
					   unsigned long *buf)
{
	void *ptr = mmap(0, handle->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			 handle->share_fd, 0);
	if (MAP_FAILED == ptr) {
		ALOGE("%s: failed to mmap for %d", __func__, handle->share_fd);
		return -EINVAL;
	}
	*buf = (unsigned long)ptr;
	return 0;
}

int Camera3BufferControl::stillCapture(int index)
{
	int ret;

	mMutex.lock();
	capture_result_t buf = mQueued[index];
	mMutex.unlock();

	if (buf.num_output_buffers == 1) {
		ALOGE("[%s] num_output_buffers:%d\n", __FUNCTION__,
		      buf.num_output_buffers);
		return -EINVAL;
	}

	camera3_stream_buffer_t *src =
		&buf.output_buffers[0];
	private_handle_t *srcBuf = (private_handle_t*)*src->buffer;
	if (srcBuf == NULL) {
		ALOGE("[%s] srcBuf is NULL\n", __FUNCTION__);
		return -EINVAL;
	}
	ALOGV("[%s] get source buffer\n", __FUNCTION__);

	camera3_stream_buffer_t *dst =
		&buf.output_buffers[1];
	private_handle_t *dstBuf = (private_handle_t*)*dst->buffer;
	if (dstBuf == NULL) {
		ALOGE("[%s] dstBuf is NULL\n", __FUNCTION__);
		return -EINVAL;
	}

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);
	android_ycbcr ycbcr_src, ycbcr_dst;

	ret = module->lock_ycbcr(module, dstBuf, PROT_READ | PROT_WRITE, 0, 0,
				 dstBuf->width, dstBuf->height, &ycbcr_dst);
	if (ret) {
		ALOGE("failed to lock_ycbcr for dst buffer");
		return -EINVAL;
	}
	ret = module->lock_ycbcr(module, srcBuf, PROT_READ | PROT_WRITE, 0, 0,
				 srcBuf->width, srcBuf->height, &ycbcr_src);
	if (ret) {
		ALOGE("failed to lock_ycbcr for src buffer");
		return -EINVAL;
	}
	ALOGV("[SRCAddr] Y:%p, Cb:%p, Cr:%p\n",
	      ycbcr_src.y, ycbcr_src.cb, ycbcr_src.cr);
	ALOGV("[DSTAddr] Y:%p, Cb:%p, Cr:%p\n",
	      ycbcr_dst.y, ycbcr_dst.cb, ycbcr_dst.cr);

	memcpy((void*)ycbcr_dst.y, (void*)ycbcr_src.y, srcBuf->size);

	ret = module->unlock(module, dstBuf);
	if (ret) {
		ALOGE("[%s] failed to gralloc unlock:%d\n", __FUNCTION__, ret);
		return ret;
	}
	ret = module->unlock(module, srcBuf);
	if (ret) {
		ALOGE("[%s] failed to gralloc unlock:%d\n", __FUNCTION__, ret);
		return ret;
	}
	return 0;
}

bool Camera3BufferControl::isCapture(int index)
{
	bool ret = false;

	mMutex.lock();
	ret = mQueued[index].capture;
	mMutex.unlock();

	return ret;
}

bool Camera3BufferControl::threadLoop(void)
{
	int ret, dma_fd;
	status_t res;
	capture_result_t	*result;
	int	 index = 0;

	ALOGV("[%s]\n", __FUNCTION__);

	if (mStreaming != STREAMING_MODE)
		goto stopStreaming;

	mMutex.lock();
	index = mQIndex;
	mMutex.unlock();

	if (index >= (MIN_BUFFER_COUNT + 1)) {

		ret = v4l2_dqbuf(mFd, &index, &dma_fd, 1);
		if (ret) {
			ALOGE("failed to dqbuf:%d\n", ret);
			goto stopStreaming;
		}
		if (mStreaming != STREAMING_MODE)
			goto stopStreaming;

		mMutex.lock();
		mDqIndex = index;
		ALOGV("DqBuf : %d, qIndex: %d, dma_fd: %d\n",
			mDqIndex, mQIndex, dma_fd);
		mMutex.unlock();

		index = getBuffer(dma_fd);
		if (index < 0) {
			ALOGE("[%s] Invalied index:%d\n", __FUNCTION__, index);
			goto stopStreaming;
		}
		/* checkResult(index);*/
		ALOGV("[%s] isCapture:%d\n",
		      __FUNCTION__, isCapture(index));
		if (isCapture(index)) {
			if (stillCapture(index))
				goto stopStreaming;
		}
		sendCaptureResult(true, index);
		removeBuffer(index, true);

		if (mReqIndex) {
			mMutex.lock();
			index = mDqIndex;
			mMutex.unlock();
			ret = moveToQueued(index);
			if (ret)
				goto stopStreaming;
		}
	} else {
		if (mReqIndex) {
			mMutex.lock();
			index = mQIndex;
			mMutex.unlock();
			ret = moveToQueued(index);
			if (ret)
				goto stopStreaming;
		}
	}

	return true;

stopStreaming:
	ret = stopStreaming();
	ALOGD("[%s] thread is stopped:%d\n", __FUNCTION__, ret);

	return false;
}

}; /* namespace android */
