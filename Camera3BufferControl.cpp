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
	v4l2_fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YVU420;
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
	mStreaming = false;
	mQIndex = 0;
	mDqIndex = 0;
	mReqIndex = 0;

	for (int i = 0; i < MAX_BUFFER_COUNT; i++) {
		memset(&mRequests[i], 0x0, sizeof(capture_result_t));
		memset(&mRequests[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t));
		memset(&mQueued[i], 0x0, sizeof(capture_result_t));
		memset(&mQueued[i].output_buffers, 0x0,
		       sizeof(camera3_stream_buffer_t));
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
		ALOGE("failed to lock_ycbcr for EGL_YV12_KHR format");
		return -EINVAL;
	}
	ret = module->unlock(module, buf);
	if (ret) {
		ALOGE("[%s] failed to gralloc unlock:%d\n", __FUNCTION__, ret);
		return ret;
	}

	uint32_t num_planes = 3;
	uint32_t y_width = buf->width;
	uint32_t y_height = buf->height;
	uint32_t strides[num_planes];
	uint32_t sizes[num_planes];

	ALOGV("[%s] ystride:%zu, cstride:%zu\n", __FUNCTION__, ycbcr.ystride,
	      ycbcr.cstride);

	strides[0] = (uint32_t)ycbcr.ystride;
	sizes[0] = (uint64_t)(ycbcr.cr) - (uint64_t)(ycbcr.y);
	strides[1] = strides[2] = (uint32_t)ycbcr.cstride;
	sizes[1] = sizes[2] = (uint64_t)ycbcr.cb - (uint64_t)ycbcr.cr;

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

	return 0;
}

bool Camera3BufferControl::getStreamingMode(void)
{
	ALOGV("[%s] streaming mode is %s\n", __FUNCTION__,
	      mStreaming ? "TRUE" : "FALSE");
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

	ALOGD("[%s] %s - frame_number : %d, timestamp : %ld\n", __FUNCTION__,
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
	else buf = mRequests[index];
	mMutex.unlock();

	ALOGD("[%s] index:%d\n", __FUNCTION__, index);

	memset(&result, 0x0, sizeof(camera3_capture_result_t));
	if (buf.output_buffers.status) {
		ALOGE("[%s] status is error, don't send result\n",
		      __FUNCTION__);
		return 0;
	}
	buf.output_buffers.release_fence  = -1;
	buf.output_buffers.acquire_fence  = -1;
	result.frame_number = buf.frame_number;
	result.result = NULL;
	result.num_output_buffers = buf.num_output_buffers;
	result.output_buffers = &buf.output_buffers;
	result.partial_result = 0;
	result.input_buffer = NULL;
	ALOGD("[%s] frame_number:%d, num_buffers:%d, buffers:%p, status:%d\n",
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
	mStreaming = false;

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
			memset(&buf[i].output_buffers, 0x0,
			       sizeof(camera3_stream_buffer_t));
			for (; i < (bufCount-1); i++) {
				memcpy(&buf[i], &buf[i+1],
				       sizeof(capture_result_t));
				memcpy(&buf[i].output_buffers,
				       &buf[i+1].output_buffers,
				       sizeof(camera3_stream_buffer_t));
			}
			memset(&buf[i], 0x0, sizeof(capture_result_t));
			memset(&buf[i].output_buffers, 0x0,
			       sizeof(camera3_stream_buffer_t));
			if (queued)
				mQIndex--;
			else
				mReqIndex--;
			break;
		}
	}
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
			ALOGD("[%s] i is %d\n", __FUNCTION__, i);
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
		memcpy(&mQueued[mQIndex].output_buffers, &buf->output_buffers,
		       sizeof(camera3_stream_buffer_t));
		mQIndex++;
		ALOGV("[%s] mQIndex: %d\n", __FUNCTION__, mQIndex);
		mMutex.unlock();
	} else {
		mMutex.lock();
		memcpy(&mRequests[mReqIndex], buf, sizeof(capture_result_t));
		memcpy(&mRequests[mReqIndex].output_buffers,
		       &buf->output_buffers, sizeof(camera3_stream_buffer_t));
		mReqIndex++;
		ALOGV("[%s] mReqIndex: %d\n", __FUNCTION__, mReqIndex);
		mMutex.unlock();
	}

	return 0;
}

int Camera3BufferControl::moveToQueued(uint32_t index)
{
	ALOGV("[%s] mReqIndex: %d\n", __FUNCTION__, mReqIndex);

	addBuffer(&mRequests[0], true, index);
	mRequests[0].timestamp = (systemTime(SYSTEM_TIME_MONOTONIC)/
				  default_frame_duration);
	sendNotify((mRequests[0].output_buffers.status) ? true: false,
		   mRequests[0].frame_number, mRequests[0].timestamp);
	removeBuffer(0, false);
	return 0;
}

int Camera3BufferControl::registerBuffer(camera3_capture_request_t *request)
{
	if (request == NULL) {
		ALOGE("request is NULL\n");
		return -EINVAL;
	}
	ALOGV("[%s] frame_number:%d, num_buffers:%d\n",
	      __FUNCTION__, request->frame_number, request->num_output_buffers);

	mMutex.lock();
	if ((mQIndex >= MAX_BUFFER_COUNT) && (mReqIndex >= MAX_BUFFER_COUNT)) {
		ALOGE("Max Buffer is already queued : %d, %d\n",
		      mQIndex, mReqIndex);
		mMutex.unlock();
		return -ENOMEM;
	}
	mMutex.unlock();

	for (uint32_t i = 0; i < request->num_output_buffers; i++) {
		const camera3_stream_buffer_t *output =
			&request->output_buffers[i];
		private_handle_t *buf =
			(private_handle_t *)*output->buffer;
		capture_result_t buffer;

		ALOGV("[%d] fd:%d, ion_hnd:%p, size:%d, width:%d, height:%d, stride:%d\n",
		      i, buf->share_fd, buf->ion_hnd, buf->size, buf->width,
		      buf->height, buf->stride);

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
			ALOGD("[%s] acquire fence is exist:%d\n", __FUNCTION__,
			      output->acquire_fence);
			int32_t rc = sync_wait(output->acquire_fence, -1);
			ALOGD("[%s] fence is released\n", __FUNCTION__);
			close(output->acquire_fence);
			if (rc != OK) {
				ALOGE("[%s] sync wait is failed : %d\n",
				      __FUNCTION__, rc);
				return rc;
			}
		}

		buffer.fd = buf->share_fd;
		buffer.frame_number = request->frame_number;
		buffer.num_output_buffers =
			request->num_output_buffers;
		buffer.timestamp =
			(systemTime(SYSTEM_TIME_MONOTONIC)/
			 default_frame_duration);
		memcpy(&buffer.output_buffers, output,
		       sizeof(camera3_stream_buffer_t));
		ALOGV("fd:%d, frameNumber:%d, num_buffers:%d, timestamp:%lld\n",
		      buffer.fd, buffer.frame_number,
		      buffer.num_output_buffers,
		      buffer.timestamp);
		mMutex.lock();
		int index = (mStreaming) ? mDqIndex: mQIndex;
		bool queued = (!mStreaming);
		mMutex.unlock();
		addBuffer(&buffer, queued, index);

		if (queued)
			sendNotify((buffer.output_buffers.status) ? true: false,
				   buffer.frame_number, buffer.timestamp);
	}
	return 0;
}

int Camera3BufferControl::checkResult(int index)
{
	void *virt;
	const camera3_stream_buffer_t *output =
		&mQueued[index].output_buffers;
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
	ALOGD("[%s] hnd:%p, fd:%d, size:%d, stride:%d\n", __FUNCTION__,
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

	ALOGD("[%s]\n", __FUNCTION__);

	if (mStreaming)
		stopStreaming();

	for (uint32_t i = 0; i < mReqIndex; i++) {
		sendNotify(true, mRequests[i].frame_number,
			   mRequests[i].timestamp);
		sendCaptureResult(false, i);
	}

	for (uint32_t i = 0; i < mQIndex; i++) {
		sendCaptureResult(true, i);
		removeBuffer(i, true);
	}

	ALOGD("[%s] Finished\n", __FUNCTION__);

	return 0;
}

status_t Camera3BufferControl::readyToRun() {

	int ret, dma_fd;

	ALOGD("[%s]\n", __FUNCTION__);

	ret = v4l2_streamon(mFd);
	if (ret) {
		ALOGE("failed to stream on:%d\n", ret);
		return ret;
	}
	mStreaming = true;

	return NO_ERROR;
}

bool Camera3BufferControl::threadLoop() {
	int ret, dma_fd;
	status_t res;
	capture_result_t	*result;
	int	 index;

	ALOGV("[%s]\n", __FUNCTION__);

	mMutex.lock();
	index = mQIndex;
	mMutex.unlock();

	if (index >= (MIN_BUFFER_COUNT + 1)) {

		ret = v4l2_dqbuf(mFd, &index, &dma_fd, 1);
		if (ret) {
			ALOGE("failed to dqbuf:%d\n", ret);
			goto error;
		}
		mMutex.lock();
		mDqIndex = index;
		ALOGD("DqBuf : %d, qIndex: %d, dma_fd: %d\n",
			mDqIndex, mQIndex, dma_fd);
		mMutex.unlock();

		if (!mStreaming)
			goto error;

		index = getBuffer(dma_fd);
		if (index < 0) {
			ALOGE("[%s] Invalied index:%d\n", __FUNCTION__, index);
			goto error;
		}

		/* checkResult(index);*/
		sendCaptureResult(true, index);
		removeBuffer(index, true);

		if (mReqIndex) {
			mMutex.lock();
			index = mDqIndex;
			mMutex.unlock();
			moveToQueued(index);
		}
	} else {
		if (mReqIndex) {
			mMutex.lock();
			index = mQIndex;
			mMutex.unlock();
			moveToQueued(index);
		}
	}

	return mStreaming;
error:
	return false;
}

}; /* namespace android */
