#ifndef NX_V4L2_H
#define NX_V4L2_H

namespace android {

int v4l2_set_format(int fd, uint32_t w, uint32_t h, uint32_t num_planes,
					uint32_t strides[], uint32_t sizes[]);
int v4l2_req_buf(int fd, int count);
int v4l2_qbuf(int fd, uint32_t index, int dma_fds[], uint32_t num_planes,
			  uint32_t sizes[]);
int v4l2_dqbuf(int fd, int *index, int32_t dma_fd[], uint32_t num_planes);
int v4l2_streamon(int fd);
int v4l2_streamoff(int fd);

}; // namespace android

#endif
