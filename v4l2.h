#ifndef NX_V4L2_H
#define NX_V4L2_H

namespace android {

enum v4l2_interval {
	V4L2_INTERVAL_MIN = 0,
	V4L2_INTERVAL_MAX,
};

struct v4l2_frame_interval {
	int32_t index;
	int32_t width;
	int32_t height;
	int32_t interval[2];
};

int v4l2_set_format(int fd, uint32_t f, uint32_t w, uint32_t h,
		    uint32_t num_planes, uint32_t strides[], uint32_t sizes[]);
int v4l2_req_buf(int fd, int count);
int v4l2_qbuf(int fd, uint32_t index, int dma_fds[], uint32_t num_planes,
			  uint32_t sizes[]);
int v4l2_dqbuf(int fd, int *index, int32_t dma_fd[], uint32_t num_planes);
int v4l2_streamon(int fd);
int v4l2_streamoff(int fd);

int v4l2_get_framesize(int fd, struct v4l2_frame_interval *f);
int v4l2_get_frameinterval(int fd, struct v4l2_frame_interval *f, int minOrMax);

}; // namespace android

#endif
