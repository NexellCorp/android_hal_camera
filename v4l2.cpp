#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <log/log.h>
#include "GlobalDef.h"
#include "v4l2.h"

#ifdef USE_V4L2_LIB
#include <nx-v4l2.h>
#endif

namespace android {

int v4l2_set_format(int fd, uint32_t f, uint32_t w, uint32_t h,
		uint32_t num_planes, uint32_t strides[], uint32_t sizes[])
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_set_fmt(fd, f, w, h, num_planes, strides, sizes);
#else
	struct v4l2_format v4l2_fmt;

	ALOGDV("[%s]\n", __func__);

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
		ALOGDV("[%d] strides=%d, size=%d\n", i, strides[i], sizes[i]);
	}

	return ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt);
#endif
}

int v4l2_req_buf(int fd, int count)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_reqbuf(fd, nx_clipper_video, count);
#else
	struct v4l2_requestbuffers req;

	bzero(&req, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_DMABUF;
	req.count = count;

	return ioctl(fd, VIDIOC_REQBUFS, &req);
#endif
}

int v4l2_qbuf(int fd, uint32_t index, int dma_fds[], uint32_t num_planes,
		uint32_t sizes[])
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_qbuf(fd, nx_clipper_video, (int)num_planes, index,
			dma_fds, (int*)sizes);
#else
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
#endif
}

int v4l2_dqbuf(int fd, int *index, int32_t dma_fd[], uint32_t num_planes)
{
#ifdef USE_V4L2_LIB
	(void)(dma_fd);
	return nx_v4l2_dqbuf(fd, nx_clipper_video, num_planes, index);
#else
	struct v4l2_buffer buf;
	struct v4l2_plane planes[num_planes];
	int ret = 0;

	ALOGDV("[%s]dqIndex", __func__);

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
#endif
}

int v4l2_streamon(int fd)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_streamon(fd, nx_clipper_video);
#else
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGDV("[%s]", __func__);
	return ioctl(fd, VIDIOC_STREAMON, &buf_type);
#endif
}

int v4l2_streamoff(int fd)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_streamoff(fd, nx_clipper_video);
#else
	uint32_t buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ALOGDV("[%s]", __func__);
	return ioctl(fd, VIDIOC_STREAMOFF, &buf_type);
#endif
}

int v4l2_get_framesize(int fd, struct v4l2_frame_info *f)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_get_framesize(fd, (struct nx_v4l2_frame_info*)f);
#else
	struct v4l2_frmsizeenum frame;
	int ret = 0;

	ALOGDI("[%s] index:%d", __func__, f->index);
	frame.index = f->index;
	ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame);
	if (!ret) {
		f->width = frame.stepwise.max_width;
		f->height = frame.stepwise.max_height;
		ALOGDV("[%s] index:%d, width:%d, height:%d", __func__,
			f->index, f->width, f->height);
	}
		ALOGDI("[%s] index:%d, width:%d, height:%d", __func__,
			f->index, f->width, f->height);
	return ret;
#endif
}

int v4l2_get_frameinterval(int fd, struct v4l2_frame_info *f, int minOrMax)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_get_frameinterval(fd, (struct nx_v4l2_frame_info*)f, minOrMax);
#else
	struct v4l2_frmivalenum frame;
	int ret;

	ALOGDV("[%s] index:%d", __func__, f->index);
	frame.index = minOrMax;
	frame.width = f->width;
	frame.height = f->height;
	ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frame);
	if (!ret) {
		f->interval[frame.index] = frame.discrete.denominator;
		ALOGDV("index:%d, width:%d, height:%d, interval:%d",
				f->index, f->width, f->height, f->interval[frame.index]);
	} else
		ALOGE("failed to get frame interval information :%d", ret);
	return ret;
#endif
}

int v4l2_get_crop(int fd, struct v4l2_crop_info *crop)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_get_crop(fd, nx_clipper_video, &crop->left, &crop->top,
			&crop->width, &crop->height);
#else
	struct v4l2_crop f;
	int ret = 0;

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_G_CROP, &f);
	if (!ret) {
		ALOGDI("[%s] crop left:%d top:%d width:%d, height:%d", __func__,
			f.c.left, f.c.top, f.c.width, f.c.height);
		if (!f.c.width || !f.c.height)
			return -EINVAL;
		crop->left = f.c.left;
		crop->top = f.c.top;
		crop->width = f.c.width;
		crop->height = f.c.height;
	} else
		ALOGDI("[%s] crop left:%d top:%d width:%d, height:%d", __func__,
			f.c.left, f.c.top, f.c.width, f.c.height);
	return ret;
#endif
}

int v4l2_set_crop(int fd, struct v4l2_crop_info *crop)
{
#ifdef USE_V4L2_LIB
	return nx_v4l2_set_crop(fd, nx_clipper_video, crop->left, crop->top,
			crop->width, crop->height);
#else
	struct v4l2_crop f;

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.c.left = crop->left;
	f.c.top = crop->top;
	f.c.width = crop->width;
	f.c.height = crop->height;
	ALOGDV("[%s] crop left:%d top:%d width:%d, height:%d", __func__,
		f.c.left, f.c.top, f.c.width, f.c.height);
	return ioctl(fd, VIDIOC_S_CROP, &f);
#endif
}

int v4l2_get_camera_type(char *video, int* mipi, int*interlaced)
{
	int ret = -EINVAL;
#ifdef USE_V4L2_LIB
	ALOGE("[%s] video:%s", __func__, video);
	ret = nx_v4l2_get_camera_type(video, mipi, interlaced);
#else
	ALOGE("[%s] can't support this function for %s, enable USE_V4L2_LIB",
			__func__, video);
#endif
	return ret;
}

}; // namespace android
