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
#ifndef CAMERA3_HW_INTERFACE_H
#define CAMERA3_HW_INTERFACE_H


namespace android {

/*****************************************************************************/
/* Camera3 Hal Interface Class                                               */
/*****************************************************************************/
class Camera3HWInterface {

public:
	Camera3HWInterface(int cameraId);
	~Camera3HWInterface();

	int initialize(const camera3_callback_ops_t *callback_ops);
	int configureStreams(camera3_stream_configuration_t *stream_list);
	const camera_metadata_t *constructDefaultRequestSettings(int type);
	int processCaptureRequest(camera3_capture_request_t *request);
	int flush();

	int cameraDeviceClose();
	int validateCaptureRequest(camera3_capture_request_t *request,
		bool firstRequest);
	int sendResult();

	camera3_device_t *getCameraDevice() {
	return &mCameraDevice;
	}

private:
	int mCameraId;
	const camera3_callback_ops_t *mCallbacks;
	int mHandles[NX_MAX_STREAM];
	int mScaler;
	int mDeinterlacer;
	camera_metadata_t *mRequestMetadata[CAMERA3_TEMPLATE_MANUAL] =
	{NULL, NULL, NULL, NULL, NULL};
	sp<StreamManager> mStreamManager;

private:
	camera3_device_t mCameraDevice;
};

} /* namespace android */

#endif /* CAMERA3_HW_INTERFACE_H */
