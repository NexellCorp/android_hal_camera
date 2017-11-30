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
#ifndef STREAM_H
#define STREAM_H

#include <utils/Condition.h>
#include <utils/Errors.h>
#include <utils/List.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>

#include <hardware/camera3.h>

#include <gralloc_priv.h>

#include "NXQueue.h"

typedef enum {
	NX_IDLE_STREAM = 0,
	NX_PREVIEW_STREAM,
	NX_CAPTURE_STREAM,
	NX_RECORD_STREAM,
	NX_MAX_STREAM,
} nx_stream_type;

namespace android {

class NXCamera3Buffer {
public:
	NXCamera3Buffer() {
	}
	virtual ~NXCamera3Buffer() {
	}
	void init (uint32_t frameNumber, camera3_stream_t *s,
		buffer_handle_t *b) {
		dbg_stream("[%s] frame_number:%d, mStream:%p, mBuffer:%p",
			__func__, frameNumber, s, b);
		mFrameNumber = frameNumber;
		mStream = s;
		mBuffer = *b;
	}
	private_handle_t *getPrivateHandle() {
		return (private_handle_t*)(mBuffer);
	}
	int getDmaFd() {
		private_handle_t *h = (private_handle_t*)(mBuffer);
		return h->share_fd;
	}
	camera3_stream_t *getStream(){
		return mStream;
	}
	buffer_handle_t *getBuffer() {
		return &mBuffer;
	}
	uint32_t getFrameNumber() {
		return mFrameNumber;
	}
	private:
	uint32_t mFrameNumber;
	camera3_stream_t *mStream;
	buffer_handle_t mBuffer;

}; /* NXCamera3Buffer */

typedef struct nx_camera3_callback_ops {
	void *priv;
	void (*capture_result)(const struct nx_camera3_callback_ops *,
			uint32_t type,
			NXCamera3Buffer *);
} nx_camera3_callback_ops_t;

class Stream : public Thread {
public:
	Stream(int fd, alloc_device_t *allocator,
			nx_camera3_callback_ops_t *cb,
			camera3_stream_t * stream,
			uint32_t type)
		: mFd(fd),
		mAllocator(allocator),
		mCb(cb),
		mFormat(stream->format),
		mWidth(stream->width),
		mHeight(stream->height),
		mUsage(stream->usage),
		mType(type),
		mSize(0),
		mQIndex(0),
		mSkip(0) {
			dbg_stream("[%s:%d] create", __func__, mType);
			for (uint32_t i = 0; i < MAX_BUFFER_COUNT + 2; i++)
				mFQ.queue(&mBuffers[i]);
		}
	virtual ~Stream() {
		dbg_stream("[%s:%d] delete", __func__, mType);
	}
	void setQIndex(int index) {
		mQIndex = index % MAX_BUFFER_COUNT;
	}
	void setMode(uint32_t type) {
		mType = type;
	}
	uint32_t getMode(void) {
		return mType;
	}
	void setHandle(uint32_t fd) {
		mFd = fd;
		dbg_stream("[%s:%d] fd is %d", __func__, mType, mFd);
	}
	int registerBuffer(uint32_t frameNumber, const camera3_stream_buffer *buf);
	void stopStreaming();
	bool isThisStream(camera3_stream_t *b);
	int allocBuffer(uint32_t w, uint32_t h, uint32_t format,
			buffer_handle_t *handle);
	int skipFrames();
protected:
	virtual status_t readyToRun();
	virtual bool threadLoop();

private:
	int mFd;
	alloc_device_t *mAllocator;
	const nx_camera3_callback_ops_t *mCb;
	uint32_t mFormat;
	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mUsage;
	uint32_t mType;
	uint32_t mSize;
	uint32_t mQIndex;
	uint32_t mSkip;
	NXQueue<NXCamera3Buffer *>mFQ;
	NXQueue<NXCamera3Buffer *>mQ;
	NXQueue<NXCamera3Buffer *>mRQ;
	Mutex mLock;
	NXCamera3Buffer mBuffers[MAX_BUFFER_COUNT+2];

	int setBufferFormat(private_handle_t *h);
	int sendResult(bool drain = false);
	void stopV4l2();
	void drainBuffer();

}; /* Stream */

}; /* namespace android */

#endif /* STREAM_H */
