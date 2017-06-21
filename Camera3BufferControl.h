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
#ifndef CAMERA3_BUFFER_CONTROL_H
#define CAMERA3_BUFFER_CONTROL_H

#include <gralloc_priv.h>

#include <utils/Condition.h>
#include <utils/Errors.h>
#include <utils/List.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>

namespace android {

#define MAX_NUM_PLANES			3
#define MAX_BUFFER_COUNT		8
#define MIN_BUFFER_COUNT		1
#define MAX_PROCESSED_STREAMS		2

enum camera_streaming_mode {
	IDLE_MODE = 0,
	STREAMING_MODE,
	STOP_MODE
};

typedef struct capture_result {
	int			fd;
	uint32_t		frame_number;
	nsecs_t			timestamp;
	uint32_t		num_output_buffers;
	bool			capture;
	camera3_stream_buffer_t output_buffers[MAX_PROCESSED_STREAMS];
} capture_result_t;

// add for planar jpeg encoding
struct ycbcr_planar {
	unsigned char *y;
	unsigned char *cb;
	unsigned char *cr;
};

/*****************************************************************************/
/* Camera3 Buffer Control Class                                               */
/*****************************************************************************/
class Camera3BufferControl : public Thread {

public:
	Camera3BufferControl(int fd,
			    const camera3_callback_ops_t *callbacs);
	~Camera3BufferControl();

	/* Thread Control */
	status_t readyToRun();
protected:
	virtual bool	threadLoop();

public:
	/* Streamming Control */
	int		initStreaming(private_handle_t *buffer);
	int		stopStreaming(void);
	uint32_t	getStreamingMode(void);
	void		setStreamingMode(uint32_t mode);
	int		stillCapture(int index);

	/* Buffer Control */
	int		registerBuffer(camera3_capture_request_t *buffer,
				       bool capture);
	int		getQIndex(void);
	int		setBufferFormat(private_handle_t *buffer);
	int		addBuffer(capture_result_t *buf, bool queued,
				  uint32_t qIndex);
	int		removeBuffer(int index, bool queued);
	int		getBuffer(int fd);
	int		moveToQueued(uint32_t index);
	int		flush(void);
	int		getVirtForHandle(private_handle_t const *handle,
					 unsigned long *buf);
	int		releaseVirtForHandle(private_handle_t const *handle,
					     unsigned long virt);
	bool		isCapture(int index);

	/* Callback Function */
	int		sendNotify(bool error, uint32_t frame_number,
				   nsecs_t timestamp);
	int		sendCaptureResult(bool queued, int index);

	/* Debugging */
	int		checkResult(int index);

public:
	int		mFd;
	uint32_t	mQIndex;
	uint32_t	mDqIndex;
	uint32_t	mReqIndex;
	uint32_t	mCapIndex;
	uint32_t	mSize;
	uint32_t	mStreaming;
	const camera3_callback_ops_t *mCallback_ops;
	capture_result_t mRequests[MAX_BUFFER_COUNT];
	capture_result_t mQueued[MAX_BUFFER_COUNT];

private:
	Mutex		mMutex;
};

} /* namespace android */

#endif /* CAMERA3_BUFFER_CONTROL_H */
