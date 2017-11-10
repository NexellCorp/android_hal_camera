#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <cutils/log.h>
#include "v4l2.h"

namespace android {

int v4l2_set_format(int fd, uint32_t f, uint32_t w, uint32_t h,
		    uint32_t num_planes, uint32_t strides[], uint32_t sizes[])
{
	struct v4l2_format v4l2_fmt;

	ALOGV("[%s]\n", __func__);

	bzero(&v4l2_fmt, sizeof(struct v4l2_format));

	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	v4l2_fmt.fmt.pix_mp.width = w;
	v4l2_fmt.fmt.pix_mp.height = h;
	v4l2_fmt.fmt.pix_mp.pixelformat = f;
	v4l2_fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	v4l2_fmt.fmt.pix_mp.num_planes = num_planes;
	for (uint32_t i = 0; i < num_planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		plane_fmt = &v4l2_fmt.fmt.pix_mp.plane_fmt[i];
		plane_fmt->sizeimage = sizes[i];
		plane_fmt->bytesperline = strides[i];
		ALOGV("[%d] strides=%d, size=%d\n", i, strides[i], sizes[i]);
	}

	return ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
}

int v4l2_req_buf(int fd, int count)
{
	struct v4l2_requestbuffers req;

	bzero(&req, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_DMABUF;
	req.count = count;

	return ioctl(fd, VIDIOC_REQBUFS, &req);
}

int v4l2_qbuf(int fd, uint32_t index, int dma_fds[], uint32_t num_planes,
			  uint32_t sizes[])
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[num_planes];

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

int v4l2_dqbuf(int fd, int *index, int32_t dma_fd[], uint32_t num_planes)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[num_planes];
	int ret;

	ALOGV("[%s]\n", __func__);

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

int v4l2_streamon(int fd)
{
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGD("[%s]\n", __func__);
	return ioctl(fd, VIDIOC_STREAMON, &buf_type);
}

int v4l2_streamoff(int fd)
{
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGD("[%s]\n", __func__);
	return ioctl(fd, VIDIOC_STREAMOFF, &buf_type);
}

int v4l2_get_framesize(int fd, struct v4l2_frame_interval *f)
{
	struct v4l2_frmsizeenum frame;
	int ret = 0;

	ALOGD("[%s] index:%d", __func__, f->index);
	frame.index = f->index;
	ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame);
	if (!ret) {
		f->width = frame.stepwise.max_width;
		f->height = frame.stepwise.max_height;
		ALOGD("[%s] index:%d, width:%d, height:%d", __func__,
		      f->index, f->width, f->height);
	} else
		ALOGE("[%s] failed to get frame size ret:%d", __func__, ret);
	return ret;
}

int v4l2_get_frameinterval(int fd, struct v4l2_frame_interval *f, int minOrMax)
{
	struct v4l2_frmivalenum frame;
	int ret;

	ALOGD("[%s] index:%d", __func__, f->index);
	frame.index = minOrMax;
	frame.width = f->width;
	frame.height = f->height;
	ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frame);
	if (!ret) {
		f->interval[frame.index] = frame.discrete.denominator;
		ALOGD("index:%d, width:%d, height:%d, interval:%d",
			f->index, f->width, f->height, f->interval[frame.index]);
	} else
		ALOGE("failed to get frame interval information :%d", ret);
	return ret;
}

}; // namespace android
