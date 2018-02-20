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
#ifndef STREAM_MANAGER_H
#define STREAM_MANAGER_H

#include "Stream.h"
#include "ExifProcessor.h"


namespace android {

typedef struct nx_camera_request {
	uint32_t frame_number;
	uint32_t num_output_buffers;
	const camera_metadata_t *meta;
	const camera3_stream_buffer_t *input_buffer;
} nx_camera_request_t;

class StreamManager : public Thread {
public:
	StreamManager(int fd[], int scaler, alloc_device_t *allocator,
			const camera3_callback_ops_t *callback)
		: mScaler(scaler),
		mAllocator(allocator),
		mCb(callback),
		mNumBuffers(0),
		mPipeLineDepth(0),
		mMeta(NULL) {
			ALOGDD("[%s] Create", __func__);
			for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
				mFd[i] = fd[i];
				ALOGDD("[%s] fd[%d]=%d", __func__, i, mFd[i]);
			}
			mResultCb.priv = this;
			mResultCb.capture_result =
				&StreamManager::getCaptureResult;
			for (int i = 0; i < NX_MAX_STREAM; i++)
				mStream[i] = NULL;
		}
	virtual ~StreamManager() {
		ALOGDD("[%s] Delete", __func__);
	}

	int getPreviewStream(camera3_stream_configuration_t *stream_list);
	int configureStreams(camera3_stream_configuration_t *stream_list);
	int registerRequests(camera3_capture_request_t *r);
	int stopStream();
	sp<Stream> getStream(uint32_t type);

protected:
	virtual status_t readyToRun();
	virtual bool threadLoop();

private:
	static void getCaptureResult(const struct nx_camera3_callback_ops *ops,
			uint32_t type,
			NXCamera3Buffer *buf);
	void setCaptureResult(uint32_t type, NXCamera3Buffer *buf);
	int getRunningStreamsCount(void);
	void drainBuffer(void);
	int sendResult(void);
	int allocBuffer(uint32_t w, uint32_t h, uint32_t format, buffer_handle_t *p);
	int jpegEncoding(private_handle_t *dst, private_handle_t *src,
			exif_attribute_t *exif);
	int scaling(private_handle_t *src, private_handle_t *dst,
			const camera_metadata_t *request);
	int copyBuffer(private_handle_t *dst, private_handle_t *src);
	int runStreamThread(camera3_stream_t *s);
	private_handle_t* getSimilarActiveStream(camera3_stream_buffer_t *out,
			int num_buffers, camera3_stream_t *s);

private:
	int mFd[MAX_VIDEO_HANDLES];
	int mScaler;
	alloc_device_t *mAllocator;
	ExifProcessor mExifProcessor;
	const camera3_callback_ops_t * mCb;
	nx_camera3_callback_ops_t mResultCb;
	NXQueue<NXCamera3Buffer *> mResultQ[NX_MAX_STREAM];
	NXQueue<NXCamera3Buffer *> mRQ;
	NXQueue<nx_camera_request_t *> mRequestQ;
	buffer_handle_t mScaleBuf;
	uint32_t mNumBuffers;
	uint8_t mPipeLineDepth;
	const camera_metadata_t *mMeta;
	sp<Stream> mStream[NX_MAX_STREAM];

}; /* StreamManager */

}; /* namespace android */

#endif /* STREAM_MANAGER_H */
