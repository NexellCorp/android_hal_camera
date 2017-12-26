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
} nx_camera_request_t;

class StreamManager : public Thread {
public:
	StreamManager(int fd[], alloc_device_t *allocator,
			const camera3_callback_ops_t *callback)
		: mAllocator(allocator),
		mCb(callback),
		mMode(0),
		mNumBuffers(0),
		mPipeLineDepth(0) {
			ALOGD("[%s] Create", __func__);
			for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
				mFd[i] = fd[i];
				dbg_stream("[%s] fd[%d]=%d", __func__, i, mFd[i]);
			}
			mResultCb.priv = this;
			mResultCb.capture_result =
				&StreamManager::getCaptureResult;
			for (int i = 0; i < NX_RECORD_STREAM; i++)
				mStream[i] = NULL;
		}
	virtual ~StreamManager() {
		ALOGD("[%s] Delete", __func__);
	}

	int configureStreams(camera3_stream_configuration_t *stream_list);
	int registerRequests(camera3_capture_request_t *r);
	int stopStream();
	sp<Stream> getStream(uint32_t type, camera3_stream_t *ph, int usage);

protected:
	virtual status_t readyToRun();
	virtual bool threadLoop();

private:
	static void getCaptureResult(const struct nx_camera3_callback_ops *ops,
			uint32_t type,
			NXCamera3Buffer *buf);
	void setCaptureResult(uint32_t type, NXCamera3Buffer *buf);
	void drainBuffer();
	int sendResult(bool drain = false);
	int jpegEncoding(private_handle_t *dst, private_handle_t *src,
			exif_attribute_t *exif);
	int copyBuffer(private_handle_t *dst, private_handle_t *src);
	camera_metadata_t* translateMetadata(const camera_metadata_t *request,
			exif_attribute_t *exif,
			nsecs_t timestamp,
			uint8_t pipeline_depth);

private:
	int mFd[MAX_VIDEO_HANDLES];
	alloc_device_t *mAllocator;
	ExifProcessor mExifProcessor;
	const camera3_callback_ops_t * mCb;
	nx_camera3_callback_ops_t mResultCb;
	NXQueue<NXCamera3Buffer *> mResultQ[NX_MAX_STREAM];
	NXQueue<NXCamera3Buffer *> mRQ;
	NXQueue<nx_camera_request_t *> mRequestQ;
	uint32_t mMode;
	uint32_t mNumBuffers;
	uint8_t mPipeLineDepth;
	sp<Stream> mStream[NX_MAX_STREAM];

}; /* StreamManager */

}; /* namespace android */

#endif /* STREAM_MANAGER_H */
