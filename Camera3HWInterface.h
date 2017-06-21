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

#include "Camera3BufferControl.h"

namespace android {

#define default_frame_duration 33333333L // 1/30 s

/*****************************************************************************/
/* Camera3 Hal Interface Class                                               */
/*****************************************************************************/
class Camera3HWInterface {

public:
	Camera3HWInterface(int cameraId, int fd);
	~Camera3HWInterface();

	static const camera_metadata *initStaticMetadata(int cameraId);
	static int validateCaptureRequest(camera3_capture_request_t *request,
					  bool firstRequest);

	/* callbacks */
	int sendUrgentResult(camera3_capture_request_t *request,
			     int8_t trigger, int32_t trigger_id);

	/* public static functions called by camera service */
	static int initialize(const struct camera3_device *device,
			      const camera3_callback_ops_t *callback_ops);

	static int configureStreams(const struct camera3_device *device,
				    camera3_stream_configuration_t *stream_list);

	static const camera_metadata_t *constructDefaultRequestSettings(const struct
									camera3_device *device,
									int type);

	static int processCaptureRequest(const struct camera3_device *device,
					 camera3_capture_request_t *request);

	static int flush(const struct camera3_device *device);

	static int cameraDeviceClose(struct hw_device_t* device);

public:
	camera3_device_t mCameraDevice;

private:
	int mHandle;
	int mCameraId;
	const camera3_callback_ops_t *mCallbacks;
	sp<Camera3BufferControl> mBufControl;
};

}

#endif /* CAMERA3_HW_INTERFACE_H */
