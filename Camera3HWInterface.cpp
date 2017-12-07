#define LOG_TAG "NXCamera3HWInterface"
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

#include "GlobalDef.h"
#include "v4l2.h"
#include "StreamManager.h"
#include "Camera3HWInterface.h"

#define getPriv(dev) ((Camera3HWInterface *)(((camera3_device_t *)dev)->priv))
#define NSEC_PER_33MSEC 33000000LL

namespace android {

#include "metadata.cpp"

const camera_metadata_t *gStaticMetadata[NUM_OF_CAMERAS] = {0, };

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
		.initialize			    = initialize,
		.configure_streams		    = configureStreams,
		.construct_default_request_settings = constructDefaultRequestSettings,
		.process_capture_request	    = processCaptureRequest,
		.flush				    = flush,
		.register_stream_buffers	    = NULL,
		.dump				    = NULL,
		.get_metadata_vendor_tag_ops	    = NULL,
		.reserved			    = {0,},
	};
}

static int cameraClose(struct hw_device_t* device);

Camera3HWInterface::Camera3HWInterface(int cameraId)
	: mCameraId(cameraId),
	mCallbacks(NULL),
	mAllocator(NULL),
	mStreamManager(NULL)
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
	ALOGI("[%s] destroyed", __func__);
}

int Camera3HWInterface::initialize(const camera3_callback_ops_t *callback)
{
	int fd;

	ALOGD("[%s] mCameraId:%d", __func__, mCameraId);

	if (mCameraId == 0)
		fd = open(BACK_CAMERA_DEVICE, O_RDWR);
	else
		fd = open(FRONT_CAMERA_DEVICE, O_RDWR);
	if (fd < 0) {
		ALOGE("Failed to open %s camera :%d",
				(mCameraId) ? "Front"  : "Back", fd);
		return -ENODEV;
	}
	mHandles[0] = fd;
	ALOGD("mHandle[0] = %d", fd);
	if (MAX_VIDEO_HANDLES > 1) {
		if (mCameraId == 0)
			fd = open(FRONT_CAMERA_DEVICE, O_RDWR);
		else
			fd = open(BACK_CAMERA_DEVICE, O_RDWR);
		if (fd < 0) {
			ALOGE("Failed to open %s camera :%d",
					(!mCameraId) ? "Front"  : "Back", fd);
			return -ENODEV;
		}
		mHandles[1] = fd;
		ALOGD("mHandle[1] = %d", fd);
	}
	mCallbacks = callback;
	for (int j = 0; j < CAMERA3_TEMPLATE_MANUAL; j++) {
		mRequestMetadata[j] = NULL;
		ALOGD("mRequestMetadata[%d] = %p\n", j, mRequestMetadata[j]);
	}

	if (mAllocator == NULL) {
		hw_device_t *dev = NULL;
		alloc_device_t *device = NULL;
		hw_module_t const *pmodule = NULL;
		gralloc_module_t const *module = NULL;
		hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
		module = reinterpret_cast<gralloc_module_t const *>(pmodule);
		module->common.methods->open(pmodule, GRALLOC_HARDWARE_GPU0, &dev);
		if (dev == NULL) {
			ALOGE("Failed to open alloc device");
			return -ENODEV;
		}
		device = reinterpret_cast<alloc_device_t *>(dev);
		mAllocator = device;
	}
	return 0;
}

int Camera3HWInterface::configureStreams(camera3_stream_configuration_t *stream_list)
{
	if ((stream_list == NULL) || (stream_list->streams == NULL)) {
		ALOGE("[%s] stream configurationg is NULL", __func__);
		return -EINVAL;
	}

	ALOGD("[%s] operation_mode:%d, num_streams:%d", __func__,
			stream_list->operation_mode, stream_list->num_streams);
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
			if (new_stream->format == HAL_PIXEL_FORMAT_BLOB)
				new_stream->max_buffers = 1;
			else {
				new_stream->usage |= GRALLOC_USAGE_HW_CAMERA_WRITE;
				new_stream->max_buffers = MAX_BUFFER_COUNT;
			}
		}
		ALOGD("[%s] stream type = %d, max_buffer = %d, usage = 0x%x",
				__func__, new_stream->stream_type, new_stream->max_buffers,
				new_stream->usage);
	}

	if (mStreamManager != NULL) {
		ALOGD("==================================================");
		mStreamManager->stopStream();
		// TODO: check destructor
		mStreamManager = NULL;
	}

	if (mStreamManager == NULL) {
		ALOGD("new Stream");
		ALOGD("==================================================");
		mStreamManager = new StreamManager(mHandles, mAllocator, mCallbacks);
		if (mStreamManager == NULL) {
			ALOGE("Failed to construct StreamManager for preview");
			return -ENOMEM;
		}
		mStreamManager->configureStreams(stream_list);
	}
	return 0;
}

const camera_metadata_t*
Camera3HWInterface::constructDefaultRequestSettings(int type)
{
	ALOGD("[%s] type = %d", __func__, type);

	if (mRequestMetadata[type-1] != NULL) {
		ALOGD("mRequestMetadata for %d is already exist", type);
		return mRequestMetadata[type-1];
	}

	CameraMetadata metaData;

	uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
	metaData.update(ANDROID_REQUEST_TYPE, &requestType, 1);

	int32_t defaultRequestID = 0;
	metaData.update(ANDROID_REQUEST_ID, &defaultRequestID, 1);

	uint8_t controlIntent = 0;
	uint8_t antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
	uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
	uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
	uint8_t controlMode = ANDROID_CONTROL_MODE_OFF;
	uint8_t aeMode = ANDROID_CONTROL_AE_MODE_OFF;
	uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
	uint8_t focusMode = ANDROID_CONTROL_AF_MODE_OFF;
	uint8_t	edge_mode = ANDROID_EDGE_MODE_FAST;
	uint8_t	colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
	uint8_t vsMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
	uint8_t optStabMode;
	uint8_t cacMode;

	switch (type) {
		case CAMERA3_TEMPLATE_PREVIEW:
			ALOGD("[%s] CAMERA3_TEMPLATE_PREVIEW:%d", __func__, type);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
			antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
			focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
			break;

		case CAMERA3_TEMPLATE_STILL_CAPTURE:
			ALOGD("[%s] CAMERA3_TEMPLATE_STILL_CAPTURE:%d", __func__, type);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
			focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
			break;

		case CAMERA3_TEMPLATE_VIDEO_RECORD:
			ALOGD("[%s] CAMERA3_TEMPLATE_VIDEO_RECORD:%d", __func__, type);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
			focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
			break;

		case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
			ALOGD("[%s] CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:%d", __func__, type);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
			focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
			break;

		case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
			ALOGD("[%s] CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:%d", __func__, type);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
			focusMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
			break;

		case CAMERA3_TEMPLATE_MANUAL:
			ALOGD("[%s] CAMERA3_TEMPLATE_MANUAL:%d", __func__, type);
			controlIntent =ANDROID_CONTROL_CAPTURE_INTENT_MANUAL;
			//awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
			controlMode = ANDROID_CONTROL_MODE_OFF;
			focusMode = ANDROID_CONTROL_AF_MODE_OFF;
			aeMode = ANDROID_CONTROL_AE_MODE_OFF;
			antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
			break;
		default:
			ALOGD("[%s] not supported", __func__);
			controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
			//awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
			controlMode = ANDROID_CONTROL_MODE_OFF;
			focusMode = ANDROID_CONTROL_AF_MODE_OFF;
			aeMode = ANDROID_CONTROL_AE_MODE_OFF;
			antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
			cacMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
			colorMode = ANDROID_COLOR_CORRECTION_MODE_TRANSFORM_MATRIX;
			optStabMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
			break;
	}

	metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);
	metaData.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibandingMode, 1);
	focusMode = ANDROID_CONTROL_AF_MODE_OFF;
	metaData.update(ANDROID_CONTROL_AF_MODE, &focusMode, 1);
	metaData.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
	metaData.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
	aeMode = ANDROID_CONTROL_AE_MODE_OFF;
	metaData.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
	metaData.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
	metaData.update(ANDROID_CONTROL_MODE, &controlMode, 1);
	//metaData.update(ANDROID_EDGE_MODE, &edge_mode, 1);
	metaData.update(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &cacMode, 1);
	//metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorMode, 1);
	metaData.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);
	metaData.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vsMode, 1);

	/* Face Detect Mode */
	/* For CTS : android.hardware.camera2.cts.CameraDeviceTest#testCameraDeviceManualTemplate */
	uint8_t facedetect_mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
	metaData.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &facedetect_mode, 1);

	/*precapture trigger*/
	uint8_t precapture_trigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
	metaData.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &precapture_trigger, 1);

	/*af trigger*/
	uint8_t af_trigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
	metaData.update(ANDROID_CONTROL_AF_TRIGGER, &af_trigger, 1);

	uint8_t  shadingMapMode = ANDROID_SHADING_MODE_OFF;
	metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingMapMode, 1);

	int32_t exposureCompensation = 0;
	metaData.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
			&exposureCompensation, 1);

	/*flash */
	uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
	metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);

	/*effect mode*/
	uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
	metaData.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

	/*scene mode*/
	uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY;
	metaData.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

	/*test pattern mode*/
	int32_t testpatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
	metaData.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &testpatternMode, 1);
	if (gStaticMetadata[mCameraId] == NULL) {
		ALOGD("gStaticMetadata for %d is not initialized", mCameraId);
		mRequestMetadata[type-1] = metaData.release();
		return mRequestMetadata[type-1];
	}

	/* metadata from static metadata */
	CameraMetadata meta;

	meta = gStaticMetadata[mCameraId];

	float default_focal_length = 0;
	if (meta.exists(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS))
		default_focal_length = meta.find(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS).data.f[0];
	ALOGD("ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS:%f", default_focal_length);
	metaData.update(ANDROID_LENS_FOCAL_LENGTH, &default_focal_length, 1);

	/* Fps range */
	/* For CTS : android.hardware.camera2.cts.CameraDeviceTest#testCameraDeviceManualTemplate */
	/*target fps range: use maximum range for picture, and maximum fixed range for video*/
	int32_t available_fps_ranges[2] = {0, 0};
	if (meta.exists(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)) {
		if ((type == CAMERA3_TEMPLATE_VIDEO_RECORD) || (type == CAMERA3_TEMPLATE_VIDEO_SNAPSHOT)) {
			available_fps_ranges[0] =
				meta.find(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).data.i32[2];
			available_fps_ranges[1] =
				meta.find(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).data.i32[3];
		} else {
			available_fps_ranges[0] =
				meta.find(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).data.i32[0];
			available_fps_ranges[1] =
				meta.find(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES).data.i32[1];
		}
	}
	ALOGD("available_fps_ranges min:%d, max:%d", available_fps_ranges[0], available_fps_ranges[1]);
	metaData.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, available_fps_ranges, 2);
	/*scaler crop region*/
	int32_t sizes[4] = {0, 0, 0, 0};
	if (meta.exists(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE)) {
		sizes[0] = meta.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[0];
		sizes[1] = meta.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[1];
		sizes[2] = meta.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[2];
		sizes[3] = meta.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[3];
	}
	ALOGD("ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE-%d:%d:%d:%d",
			sizes[0], sizes[1], sizes[2], sizes[3]);
	metaData.update(ANDROID_SCALER_CROP_REGION, sizes, 4);

	/* frame duration */
	int64_t default_frame_duration = NSEC_PER_33MSEC;
	metaData.update(ANDROID_SENSOR_FRAME_DURATION, &default_frame_duration, 1);

	mRequestMetadata[type-1] = metaData.release();

	return mRequestMetadata[type-1];
}

int Camera3HWInterface::sendResult(void)
{
	camera3_notify_msg_t msg;

	msg.type = CAMERA3_MSG_ERROR;
	msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
	msg.message.error.error_stream = NULL;
	msg.message.error.frame_number = 0;
	mCallbacks->notify(mCallbacks, &msg);

	return 0;
}

int Camera3HWInterface::validateCaptureRequest(camera3_capture_request_t * request,
		bool firstRequest)
{
	int ret = -EINVAL;
	const camera3_stream_buffer_t *buf;

	ALOGD("[%s]", __func__);

	if (request == NULL) {
		ALOGE("[%s] capture request is NULL", __func__);
		return ret;
	}

	if ((firstRequest) && (request->settings == NULL)) {
		ALOGE("[%s] meta info can't be NULL for the first request",
				__func__);
		return ret;
	}

	if ((request->num_output_buffers < 1) ||
			(request->output_buffers == NULL)) {
		ALOGE("[%s] output buffer is NULL", __FUNCTION__);
		return ret;
	}

	for (uint32_t i = 0; i < request->num_output_buffers; i++) {
		buf = request->output_buffers + i;
		if (buf->release_fence != -1) {
			ALOGE("[Buffer:%d] release fence is not -1", i);
			return ret;
		}
		if (buf->status != CAMERA3_BUFFER_STATUS_OK) {
			ALOGE("[Buffer:%d] status is not OK\n", i);
			sendResult();
			return ret;
		}
		if (buf->buffer == NULL) {
			ALOGE("[Buffer:%d] buffer handle is NULL\n", i);
			return ret;
		}
		if (*(buf->buffer) == NULL) {
			ALOGE("[Buffer:%d] private handle is NULL\n", i);
			return ret;
		}
	}
	return 0;
}

int Camera3HWInterface::processCaptureRequest(camera3_capture_request_t *request)
{
	int ret = NO_ERROR;
	bool firstRequest = (mStreamManager != NULL) ? false: true;

	ret = validateCaptureRequest(request, firstRequest);
	if (ret) {
		sendResult();
		return ret;
	}

	if (request->input_buffer != NULL)
		ALOGE("We can't support input buffer!!!");

	if (mStreamManager != NULL) {
		ret = mStreamManager->registerRequests(request);
		if (ret) {
			ALOGE("Failed to registerRequest for preview");
			return ret;
		}
	}
	return ret;
}

int Camera3HWInterface::flush()
{
	ALOGD("[%s]", __func__);
	return 0;
}

int Camera3HWInterface::cameraDeviceClose()
{
	ALOGD("[%s]", __func__);
	if ((mStreamManager != NULL) && (mStreamManager->isRunning()))
		mStreamManager->stopStream();
	if (mAllocator)
		mAllocator->common.close((struct hw_device_t *)mAllocator);


	for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
		if (mHandles[i] > 0)
			close(mHandles[i]);
		mHandles[i] = -1;
	}

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
	/*
	* TODO: need to implement a case when we support cameras more than 1
	* Currently only support one camera
	*/
	ALOGI("[%s] num of cameras:%d", __func__, NUM_OF_CAMERAS);
	return NUM_OF_CAMERAS;
}

static int getCameraInfo(int camera_id, struct camera_info *info)
{
	int ret = 0;
	int fd;

	ALOGD("[%s] cameraID:%d", __func__, camera_id);

	if (camera_id >= NUM_OF_CAMERAS || !info || (camera_id < 0))
		return -ENODEV;

	if (camera_id == 0)
		fd = open(BACK_CAMERA_DEVICE, O_RDWR);
	else
		fd = open(FRONT_CAMERA_DEVICE, O_RDWR);
	if (fd < 0) {
		ALOGE("Failed to open %s camera :%d",
				(camera_id) ? "Front"  : "Back", fd);
		return -ENODEV;
	}

	/* 0 = BACK, 1 = FRONT */
	info->facing = camera_id ? CAMERA_FACING_FRONT : CAMERA_FACING_BACK;

	/* The values is not available in the other case */
	/* TODO: set orientation by camera sensor
	*/
	if (info->facing != CAMERA_FACING_EXTERNAL)
		info->orientation = 0;
	info->device_version = CAMERA_DEVICE_API_VERSION_3_4;
	info->resource_cost = 100;
	info->conflicting_devices = NULL;
	info->conflicting_devices_length = 0;

	if (gStaticMetadata[camera_id] == NULL)
		gStaticMetadata[camera_id] = ::android::android::initStaticMetadata(camera_id, fd);
	info->static_camera_characteristics = gStaticMetadata[camera_id];

	ALOGI("======camera info =====\n");
	ALOGI("camera facing = %s\n", info->facing ? "Front" : "Back");
	ALOGI("device version = %d\n", info->device_version);
	ALOGI("resource cost = %d\n", info->resource_cost);
	ALOGI("conflicting devices is %s\n", info->conflicting_devices ? "exist"
			: "not exist");

	close(fd);

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
		ALOGE("[%s] Failed to create Camera3HWInterface", __func__);
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

} /* namespace android */
