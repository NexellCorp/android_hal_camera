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
#include "ExifProcessor.h"
#include "v4l2.h"

namespace android {

class NXCamera3Buffer {
public:
	NXCamera3Buffer() {
	}
	virtual ~NXCamera3Buffer() {
	}
	void init (uint32_t frameNumber, camera3_stream_t *s,
		buffer_handle_t *b, buffer_handle_t z,
		const camera_metadata_t *meta) {
		ALOGDI("[%s] frame_number:%d, mStream:%p, mBuffer:%p",
			__func__, frameNumber, s, b);
		mFrameNumber = frameNumber;
		mStream = s;
		mBuffer = *b;
		mZmBuffer = z;
		mMeta = meta;
	}
	void init (uint32_t frameNumber, camera3_stream_t *s,
		buffer_handle_t *b,
		const camera_metadata_t *meta) {
		ALOGDI("[%s] frame_number:%d, mStream:%p, mBuffer:%p",
			__func__, frameNumber, s, b);
		mFrameNumber = frameNumber;
		mStream = s;
		mBuffer = *b;
		mMeta = meta;
	}
	private_handle_t *getPrivateHandle() {
		return (private_handle_t*)(mBuffer);
	}
	private_handle_t *getZoomPrivateHandle() {
		return (private_handle_t*)(mZmBuffer);
	}
	int getDmaFd() {
		private_handle_t *h = (private_handle_t*)(mBuffer);
		return h->share_fd;
	}
	int getZoomDmaFd() {
		private_handle_t *h = (private_handle_t*)(mZmBuffer);
		return h->share_fd;
	}
	camera3_stream_t *getStream(){
		return mStream;
	}
	buffer_handle_t *getBuffer() {
		return &mBuffer;
	}
	buffer_handle_t *getZoomBuffer() {
		return &mZmBuffer;
	}
	uint32_t getFrameNumber() {
		return mFrameNumber;
	}
	const camera_metadata_t *getMetadata() {
		return mMeta;
	}
private:
	uint32_t mFrameNumber;
	camera3_stream_t *mStream;
	buffer_handle_t mBuffer;
	buffer_handle_t mZmBuffer;
	const camera_metadata_t *mMeta;

}; /* NXCamera3Buffer */

typedef struct nx_camera3_callback_ops {
	void *priv;
	void (*capture_result)(const struct nx_camera3_callback_ops *,
			uint32_t type,
			NXCamera3Buffer *);
} nx_camera3_callback_ops_t;

class Stream : public Thread {
public:
	Stream(uint32_t id, int fd, int scaler, alloc_device_t *allocator,
			nx_camera3_callback_ops_t *cb,
			camera3_stream_t * stream,
			uint32_t type)
		: mCameraId(id),
		mFd(fd),
		mScaler(scaler),
		mAllocator(allocator),
		mCb(cb),
		mStream(stream),
		mTmpBuf(NULL),
		mType(type),
		mSize(0),
		mQIndex(0),
		mMaxBufIndex(0),
		mSkip(0),
		mScaling(0),
		mCrop(0) {
			for (uint32_t i = 0; i < MAX_BUFFER_COUNT + 2; i++) {
				if (i < MAX_BUFFER_COUNT)
					mZmBuf[i] = NULL;
				mFQ.queue(&mBuffers[i]);
			}
#if defined(CAMERA_SUPPORT_SCALING)
			if (!isSupportedResolution(mCameraId, getWidth(), getHeight()))
				mScaling = true;
#endif
			mCrop = getCropInfo(mCameraId, &mCropInfo);
			ALOGDD("[%s:%d] create - scaling is %s, crop is %s", __func__, mType,
					(mScaling) ? "enabled" : "disabled",
					(mCrop) ? "enabled" : "disabled");
		}
	virtual ~Stream() {
		ALOGDD("[%s:%d] delete", __func__, mType);
	}
	void setQIndex(int index) {
		mQIndex = index % mMaxBufIndex;
	}
	uint32_t getQIndex() {
		return mQIndex;
	}
	void setSkipFlag(bool skip) {
		mSkip = skip;
	}
	bool getSkipFlag(void) {
		return mSkip;
	}
	uint32_t getMode(void) {
		return mType;
	}
	uint32_t getFormat(void) {
		return mStream->format;
	}
	uint32_t getWidth(void) {
		return mStream->width;
	}
	uint32_t getHeight(void) {
		return mStream->height;
	}
	void* getStream(void) {
		return this;
	}
	void setHandle(uint32_t fd) {
		mFd = fd;
		ALOGDV("[%s:%d] fd is %d", __func__, mType, mFd);
	}
	int allocBuffer(uint32_t w, uint32_t h, uint32_t format,
			buffer_handle_t *handle);
	int registerBuffer(uint32_t frameNumber, const camera3_stream_buffer *buf,
			const camera_metadata_t *meta);
	void stopStreaming();
	bool isThisStream(camera3_stream_t *b);
	bool calScalingFactor(const camera_metadata_t *meta, uint32_t *crop);
	int scaling(private_handle_t *dst, private_handle_t *src,
			const camera_metadata_t *meta);
	int jpegEncoding(private_handle_t *dst, private_handle_t *src,
			exif_attribute_t *exif);
	int skipFrames();
	int dQBuf(int *fd);
	int qBuf(NXCamera3Buffer *buf);
	status_t prepareForRun();
protected:
	virtual status_t readyToRun() {
		return NO_ERROR;
	};
	virtual bool threadLoop();

private:
	uint32_t mCameraId;
	int mFd;
	int mScaler;
	alloc_device_t *mAllocator;
	ExifProcessor mExifProcessor;
	const nx_camera3_callback_ops_t *mCb;
	const camera3_stream_t * mStream;
	buffer_handle_t mTmpBuf;
	buffer_handle_t mZmBuf[MAX_BUFFER_COUNT];
	uint32_t mType;
	uint32_t mSize;
	uint32_t mQIndex;
	uint32_t mMaxBufIndex;
	bool mSkip;
	bool mScaling;
	bool mCrop;
	NXQueue<NXCamera3Buffer *>mFQ;
	NXQueue<NXCamera3Buffer *>mQ;
	NXQueue<NXCamera3Buffer *>mRQ;
	Mutex mLock;
	NXCamera3Buffer mBuffers[MAX_BUFFER_COUNT+2];
	struct v4l2_crop_info mCropInfo;

	int setBufferFormat(private_handle_t *h);
	int sendResult();
	void stopV4l2();
	void drainBuffer();

}; /* Stream */

}; /* namespace android */

#endif /* STREAM_H */
