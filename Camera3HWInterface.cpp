#define LOG_TAG "Camera3HWInterface"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/camera.h>
#include <hardware/camera3.h>
#include <camera/CameraMetadata.h>

#include "Camera3HWInterface.h"

#define getPriv(dev) ((Camera3HWInterface *)(((camera3_device_t *)dev)->priv))

namespace android {

#include "metadata.cpp"

/**
 * Camera3 callback ops
 */
static int initialize(const struct camera3_device *device,
					  const camera3_callback_ops_t *callback_ops)
{
	Camera3HWInterface *hw = getPriv(device);
	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized", __func__);
		return -ENODEV;
	}
	return hw->initialize(callback_ops);
}

/*
 * Override camera3_stream attributes
 * Currently only override max_buffers
 */
static int configureStreams(const struct camera3_device *device,
							camera3_stream_configuration_t *stream_list)
{
	Camera3HWInterface *hw = getPriv(device);
	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized", __func__);
		return -ENODEV;
	}
	return hw->configureStreams(stream_list);
}

static const camera_metadata_t*
constructDefaultRequestSettings(const struct camera3_device *device,
								int type)
{
	Camera3HWInterface *hw = getPriv(device);
	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized", __func__);
		return NULL;
	}
	return hw->constructDefaultRequestSettings(type);
}

static int processCaptureRequest(const struct camera3_device *device,
								 camera3_capture_request_t *request)
{
	Camera3HWInterface *hw = getPriv(device);
	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized", __func__);
		return -ENODEV;
	}
	return hw->processCaptureRequest(request);
}

static int flush(const struct camera3_device *device)
{
	Camera3HWInterface *hw  = getPriv(device);
	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized", __func__);
		return -ENODEV;
	}
	return hw->flush();
}

extern "C" {
camera3_device_ops_t camera3Ops = {
	.initialize							= initialize,
	.configure_streams					= configureStreams,
	.construct_default_request_settings = constructDefaultRequestSettings,
	.process_capture_request			= processCaptureRequest,
	.flush								= flush,
	.register_stream_buffers			= NULL,
	.dump								= NULL,
	.get_metadata_vendor_tag_ops		= NULL,
	.reserved							= {0,},
};
}

static int cameraClose(struct hw_device_t* device);

Camera3HWInterface::Camera3HWInterface(int cameraId)
	: mCameraId(cameraId),
	mCallbacks(NULL),
	mPreviewHandle(-1),
	mPreviewManager(NULL)
{
	memset(&mCameraDevice, 0x0, sizeof(camera3_device_t));

	mCameraDevice.common.tag = HARDWARE_DEVICE_TAG;
	mCameraDevice.common.version = CAMERA_DEVICE_API_VERSION_3_4;
	mCameraDevice.common.close = cameraClose;
	mCameraDevice.ops = &camera3Ops;
	mCameraDevice.priv = this;

	ALOGI("cameraId = %d", cameraId);
	ALOGI("tag = %d", mCameraDevice.common.tag);
	ALOGI("version = %d", mCameraDevice.common.version);
}

Camera3HWInterface::~Camera3HWInterface(void)
{
	if (mPreviewHandle > 0)
		close(mPreviewHandle);

	ALOGI("[%s] destroyed", __func__);
}

int Camera3HWInterface::initialize(const camera3_callback_ops_t *callback)
{
	int fd;

	if (mCameraId == 0)
		fd = open(BACK_CAMERA_DEVICE, O_RDWR);
	else
		fd = open(FRONT_CAMERA_DEVICE, O_RDWR);
	if (fd < 0) {
		ALOGE("Failed to open %s camera :%d",
			(mCameraId) ? "Front"  : "Back", fd);
		return -ENODEV;
	}
	mPreviewHandle = fd;

	mCallbacks = callback;
	return 0;
}

int Camera3HWInterface::configureStreams(camera3_stream_configuration_t *stream_list)
{
	if ((stream_list == NULL) || (stream_list->streams == NULL)) {
		ALOGE("[%s] stream configurationg is NULL", __func__);
		return -EINVAL;
	}

	ALOGV("[%s] num_streams:%d", __func__, stream_list->num_streams);
	for (size_t i = 0; i < stream_list->num_streams; i++) {
		camera3_stream_t *new_stream = stream_list->streams[i];

		if (new_stream->rotation) {
			ALOGE("[%s] rotation is not supported:%d", __func__,
				  new_stream->rotation);
			return -EINVAL;
		}

		if ((new_stream->stream_type == CAMERA3_STREAM_OUTPUT) ||
			(new_stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL)) {
			ALOGD("[%zu] format:0x%x, width:%d, height:%d, max buffers:%d, usage:0x%x",
				  i, new_stream->format, new_stream->width, new_stream->height,
				  new_stream->max_buffers,
				  new_stream->usage);
			new_stream->max_buffers = MAX_BUFFER_COUNT;
		}
		ALOGD("[%s] stream type = %d, max_buffer = %d",
			  __func__, new_stream->stream_type,
			  new_stream->max_buffers);
	}

	return 0;
}

const camera_metadata_t*
Camera3HWInterface::constructDefaultRequestSettings(int type)
{
	CameraMetadata metaData;
	camera_metadata_t *metaInfo;

	ALOGV("[%s] type = %d", __func__, type);

	/* TODO: Why static?
	 * need to test non-static variable
	 */
	static const uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
	metaData.update(ANDROID_REQUEST_TYPE, &requestType, 1);

	/* TODO: check need to static variable? */
	int32_t defaultRequestID = 0;
	metaData.update(ANDROID_REQUEST_ID, &defaultRequestID, 1);

	uint8_t controlIntent = 0;
	uint8_t focusMode;
	uint8_t optStabMode;
	uint8_t cacMode;
	uint8_t edge_mode;
	uint8_t noise_red_mode;
	uint8_t tonemap_mode;

	switch (type) {
	case CAMERA3_TEMPLATE_PREVIEW:
		ALOGV("[%s] CAMERA3_TEMPLATE_PREVIEW", __func__);
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
		focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
		optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
		cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
		edge_mode = ANDROID_EDGE_MODE_FAST;
		noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
		tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
		break;

	case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
		ALOGV("[%s] CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG", __func__);
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
		focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
		optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
		cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
		edge_mode = ANDROID_EDGE_MODE_FAST;
		noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
		tonemap_mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
		break;

	case CAMERA3_TEMPLATE_STILL_CAPTURE:
		ALOGV("[%s] CAMERA3_TEMPLATE_STILL_CAPTURE", __func__);
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
		focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
		optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
		cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
		edge_mode = ANDROID_EDGE_MODE_FAST;
		noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
		tonemap_mode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
		break;

	case CAMERA3_TEMPLATE_VIDEO_RECORD:
		ALOGV("[%s] CAMERA3_TEMPLATE_VIDEO_RECORD", __func__);
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
		focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
		optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
		cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
		edge_mode = ANDROID_EDGE_MODE_FAST;
		noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
		tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
		break;

	case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
		focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
		optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
		cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
		edge_mode = ANDROID_EDGE_MODE_FAST;
		noise_red_mode = ANDROID_NOISE_REDUCTION_MODE_FAST;
		tonemap_mode = ANDROID_TONEMAP_MODE_FAST;
		break;

	default:
		ALOGD("[%s] not supported", __func__);
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
		break;
	}

	metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);
	metaData.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &cacMode, 1);
	metaData.update(ANDROID_CONTROL_AF_MODE, &focusMode, 1);
	metaData.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);
	metaData.update(ANDROID_TONEMAP_MODE, &tonemap_mode, 1);
	metaData.update(ANDROID_NOISE_REDUCTION_MODE, &noise_red_mode, 1);
	metaData.update(ANDROID_EDGE_MODE, &edge_mode, 1);

	/*flash */
	static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
	metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);

	static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
	metaData.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);

	static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
	metaData.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

	static const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
	metaData.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

	static const uint8_t controlMode = ANDROID_CONTROL_MODE_OFF;
	metaData.update(ANDROID_CONTROL_MODE, &controlMode, 1);

	static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
	metaData.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

	static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_OFF;
	metaData.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

	static const uint8_t sceneMode =
		ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY;
	metaData.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

	metaInfo = metaData.release();
	return metaInfo;
}

int Camera3HWInterface::processCaptureRequest(camera3_capture_request_t *request)
{
	int ret = NO_ERROR;

	if (request->input_buffer != NULL)
		ALOGE("We can't support input buffer!!!");

	CameraMetadata meta;
	meta = request->settings;
	if (meta.exists(ANDROID_REQUEST_ID)) {
		int32_t request_id = meta.find(ANDROID_REQUEST_ID).data.i32[0];
		ALOGV("[%s] requestId:%d", __func__, request_id);
	}

	/* preview, record, capture: called once per stream */
	if (meta.exists(ANDROID_CONTROL_CAPTURE_INTENT)) {
		uint8_t captureIntent =
			meta.find(ANDROID_CONTROL_CAPTURE_INTENT).data.u8[0];

		ALOGD("framenumber:%d, captureIntent:%d",
			  request->frame_number, captureIntent);

		if ((captureIntent == ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW) ||
			(captureIntent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD)) {
			/* stop is implicit */
			if ((mPreviewManager != NULL) &&
				(mPreviewManager->getIntent() != captureIntent)) {
					ALOGD("del Stream");
					ALOGD("==================================================");
					mPreviewManager->stopStreaming();
					// TODO: check destructor
					mPreviewManager = NULL;
			}
			if (mPreviewManager == NULL) {
				ALOGD("new Stream");
				ALOGD("==================================================");
				mPreviewManager = new StreamManager(mPreviewHandle, mCallbacks,
									captureIntent);
				if (mPreviewManager == NULL) {
					ALOGE("Failed to construct StreamManager for preview");
					return -ENOMEM;
				}
			}
		}
	}

	if (mPreviewManager != NULL) {
		ret = mPreviewManager->registerRequest(request);
		if (ret) {
			ALOGE("Failed to registerRequest for preview");
			return ret;
		}
	}

	return ret;
}

int Camera3HWInterface::flush()
{
	return 0;
}

int Camera3HWInterface::cameraDeviceClose()
{
	return 0;
}

/**
 * Common Camera Hal Interface
 */
static int getNumberOfCameras(void)
{
	/*
	 * only count built in camera BACK + FRONT
	 * external camera will be notified
	 * by camera_device_status_change callback
	 */
	/* TODO: need to get device camera information interface
	 * Currently hard coded.
	 * TODO: need to implement a case when we support cameras more than 1
	 * Currently only support one camera
	 */
	ALOGI("[%s] num of cameras:%d", __func__, NUM_OF_CAMERAS);
	return NUM_OF_CAMERAS;
}

static int getCameraInfo(int camera_id, struct camera_info *info)
{
	int ret = 0;

	ALOGD("[%s] cameraID:%d", __func__, camera_id);

	/* 0 = BACK, 1 = FRONT */
	info->facing = camera_id ? CAMERA_FACING_FRONT :
		CAMERA_FACING_BACK;

	/* The values is not available in the other case */
	/* TODO: set orientation by camera sensor
	 */
	if (info->facing != CAMERA_FACING_EXTERNAL)
		info->orientation = 0;

	info->device_version = CAMERA_DEVICE_API_VERSION_3_4;
	info->resource_cost = 100;
	info->conflicting_devices = NULL;
	info->conflicting_devices_length = 0;

	info->static_camera_characteristics = initStaticMetadata(camera_id);
	ALOGI("======camera info =====\n");
	ALOGI("camera facing = %s\n", info->facing ? "Front" : "Back");
	ALOGI("device version = %d\n", info->device_version);
	ALOGI("resource cost = %d\n", info->resource_cost);
	ALOGI("conflicting devices is %s\n", info->conflicting_devices ? "exist"
	      : "not exist");

	return 0;
}

static int setCallBacks(const camera_module_callbacks_t *)
{
	return 0;
}

static int cameraOpen(const struct hw_module_t *,
							const char *id,
							struct hw_device_t **device)
{
	int camera_id = 0;

	ALOGD("[%s]", __func__);

	camera_id = atoi(id);
	if ((camera_id < 0 ) || (camera_id >= getNumberOfCameras()))
		return -EINVAL;

	Camera3HWInterface *camera3Hal = new Camera3HWInterface(camera_id);
	if (camera3Hal == NULL) {
		ALOGE("[%s] failed to create Camera3HWInterface", __func__);
		return -ENOMEM;
	}

	*device = &camera3Hal->getCameraDevice()->common;
	return 0;
}

static int cameraClose(struct hw_device_t* device)
{
	Camera3HWInterface *hw  = getPriv(device);
	int ret = hw->cameraDeviceClose();
	delete hw;
	return ret;
}

static struct hw_module_methods_t camera_module_methods = {
	.open = cameraOpen,
};

extern "C" {
camera_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = CAMERA_MODULE_API_VERSION_2_4,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = CAMERA_HARDWARE_MODULE_ID,
		.name = "Camera HAL3",
		.author = "Nexell",
		.methods = &camera_module_methods,
		.dso = NULL,
		.reserved = {0,},
	},
	.get_number_of_cameras = getNumberOfCameras,
	.get_camera_info = getCameraInfo,
	.set_callbacks = setCallBacks,
	.get_vendor_tag_ops = NULL,
	.open_legacy = NULL,
	.set_torch_mode = NULL,
	.init = NULL,
	.reserved = {0,}
};
} /* extern C */

} // namespace android
