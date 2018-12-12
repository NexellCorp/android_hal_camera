#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <cutils/log.h>

#include <camera/CameraMetadata.h>

#include "GlobalDef.h"
#include "v4l2.h"
#include "metadata.h"

namespace android {

#define MAX_SYNC_LATENCY 4
#define MAX_SUPPORTED_RESOLUTION 64

int32_t pixel_array_size[NUM_OF_CAMERAS][2] = {{0, 0}, };

#ifdef CAMERA_SUPPORT_SCALING
struct v4l2_frame_info supported_lists[] = {
	{0, 176, 144, {15, 30}},/*QCIF*/
	{1, 320, 240, {15, 30}},/*QVGA*/
	{2, 352, 288, {15, 30}},/*CIF*/
	{3, 640, 480, {15, 30}},/*VGA*/
	{4, 1280, 720, {15, 30}},/*HD*/
	{5, 1920, 1080, {15, 30}},/*FHD*/
};
#endif

struct nx_sensor_info {
	struct v4l2_frame_info frames[MAX_SUPPORTED_RESOLUTION];
	struct v4l2_crop_info crop;
};

struct nx_sensor_info sensor_supported_lists[NUM_OF_CAMERAS] =
{{{{0, 0, 0, {0, }}, }, {0, 0, 0, 0}}, };

bool isSupportedResolution(uint32_t id, uint32_t width, uint32_t height)
{
	bool ret = false;
	struct nx_sensor_info *s = &sensor_supported_lists[id];

	if (s->crop.width && s->crop.height) {
		if ((width == s->crop.width) &&
				(height == s->crop.height))
			return true;
	}
	for (int i = 0; i < MAX_SUPPORTED_RESOLUTION; i ++) {
		if ((width == s->frames[i].width) &&
				(height == s->frames[i].height)) {
			ret = true;
			break;
		} else if (s->frames[i].width == 0)
			break;
	}
	return ret;
}

void getAvaliableResolution(uint32_t id, int *width, int *height)
{
	struct nx_sensor_info *s = &sensor_supported_lists[id];
	uint32_t left = 0, top = 0;
	uint32_t w = (uint32_t)*width;
	uint32_t h = (uint32_t)*height;

	if (s->crop.width && s->crop.height) {
		left = s->crop.left;
		top = s->crop.top;
	}
	for (int i = 0; i < MAX_SUPPORTED_RESOLUTION; i ++) {
		if ((w * h) < (s->frames[i].width *
				s->frames[i].height)) {
			if ((w + left <= s->frames[i].width) &&
					(h + top <= s->frames[i].height)) {
				*width = s->frames[i].width;
				*height = s->frames[i].height;
				break;
			}
		} else if (s->frames[i].width == 0)
			break;
	}
}

bool getCropInfo(uint32_t id, struct v4l2_crop_info *crop)
{
	struct v4l2_crop_info *c = &sensor_supported_lists[id].crop;

	if (!c->width || !c->height)
		return false;
	else
		memcpy(crop, c, sizeof(v4l2_crop_info));

	return true;
}

void getActiveArraySize(uint32_t id, uint32_t *width, uint32_t *height)
{
	*width = pixel_array_size[id][0];
	*height = pixel_array_size[id][1];
}

static int32_t checkMinFps(uint32_t count, struct v4l2_frame_info *f)
{
	uint32_t i;
	uint32_t min = f[0].interval[V4L2_INTERVAL_MIN];

	if (count == 1)
		return min;

	for (i = 1; i < count; i++) {
		if (f[i].interval[V4L2_INTERVAL_MIN] <
				f[i-1].interval[V4L2_INTERVAL_MIN])
			min = f[i].interval[V4L2_INTERVAL_MIN];
	}
	return min;
}

static int32_t checkMaxFps(uint32_t count, struct v4l2_frame_info *f)
{
	uint32_t i;
	uint32_t max = f[0].interval[V4L2_INTERVAL_MAX];

	if (count == 1)
		return max;

	for (i = 1; i < count; i++) {
		if (f[i].interval[V4L2_INTERVAL_MAX] >
				f[i-1].interval[V4L2_INTERVAL_MAX])
			max = f[i].interval[V4L2_INTERVAL_MAX];
	}
	return max;
}

static int32_t checkMaxJpegSize(uint32_t count, struct v4l2_frame_info *f)
{
	uint32_t i;
	uint32_t max = (f[0].width * f[0].height)*3;

	if (count == 1)
		return max;

	for (i = 1; i < count; i++) {
		if ((f[i].width * f[i].height) > (f[i-1].width * f[i-1].height)) {
			max = (f[i].width * f[i].height)*3;
		}
	}
	return max;
}

static uint32_t getFrameInfo(uint32_t id, int fd, struct nx_sensor_info *s)
{
	int r = 0, ret = 0;

	ALOGDI("[%s] Camera:%d Information", __func__, id);
	(void)(id);

	ret = v4l2_get_crop(fd, &s->crop);
	if (ret)
		ALOGDI("There is no crop info for %d camera", id);
	else
		ALOGDI("[%d] crop info left:%d, top:%d, width:%d, height:%d",
				id, s->crop.left,
				s->crop.top,
				s->crop.width,
				s->crop.height);

	for (int j = 0; j < MAX_SUPPORTED_RESOLUTION; j++) {
		s->frames[r].index = j;
		ret = v4l2_get_framesize(fd, &s->frames[r]);
		if (!ret) {
			ALOGDI("[%d] width:%d, height:%d",
			      r, s->frames[r].width, s->frames[r].height);
			for (int i = 0; i <= V4L2_INTERVAL_MAX; i++) {
				ret = v4l2_get_frameinterval(fd,
								&s->frames[r],
								i);
				if (ret) {
					ALOGE("Failed to get interval for width:%d, height:%d",
						s->frames[r].width, s->frames[r].height);
					return r;
				}
				ALOGDI("width:%d, height:%d, %s interval:%d",
					s->frames[r].width, s->frames[r].height,
					(i) ? "max":"min", s->frames[r].interval[i]);
			}
			r++;
		} else
			break;
	}
	return r;
}

camera_metadata_t *initStaticMetadata(uint32_t id, uint32_t facing,
		uint32_t orientation, uint32_t fd)
{
	CameraMetadata staticInfo;
	struct nx_sensor_info *s = &sensor_supported_lists[id];

	/* android.info: hardware level */
	uint8_t supportedHardwareLevel =
		ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
	staticInfo.update(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
			  &supportedHardwareLevel, 1);

	uint8_t control_mode = ANDROID_CONTROL_MODE_OFF;
	/* 3A(AE, AWB, AF) CONTROL_MODE
	 * OFF -3A is disabled
	 * all control for 3A is only effective if CONTROL_MODE is Auto
	 */
	staticInfo.update(ANDROID_CONTROL_MODE, &control_mode, 1);
	/* TODO: camera sensor information
	 * Need to handle camera sensor variation by scheme
	 * Currently hardcoding...
	 */
	/* TODO: get sensor orientation info from others
	 * need scheme
	 */
	int32_t sensor_orientation = orientation;
	staticInfo.update(ANDROID_SENSOR_ORIENTATION,
			  &sensor_orientation, 1);

	uint8_t lensFacing = (!facing) ?
		ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
	staticInfo.update(ANDROID_LENS_FACING, &lensFacing, 1);
	float focal_lengths = 3.43f;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
			  &focal_lengths, 1);

	/* Zoom */
	float maxZoom = 4;
	staticInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
			  &maxZoom, 1);

	/* Face Detect Mode */
	uint8_t availableFaceDetectModes[] = {
		ANDROID_STATISTICS_FACE_DETECT_MODE_OFF,
		ANDROID_STATISTICS_FACE_DETECT_MODE_FULL
		};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
			  availableFaceDetectModes,
			  sizeof(availableFaceDetectModes));
	/* Face Detect Mode */
	/* For CTS : android.hardware.camera2.cts.CameraDeviceTest#testCameraDeviceManualTemplate */
	uint8_t facedetect_mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
	staticInfo.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &facedetect_mode, 1);

	/* If FACE_DETECT_MODE is supported, max face count can't be less than 4 */
	int32_t maxFaces = 4;
	staticInfo.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
			  &maxFaces, 1);
	/* End Face Detect Mode */

	/* TODO: use v4l2 enum format for sensor
	 * Currently use hard coding
	 */
	/* Size */
	float physical_size[2] = {3.20f, 2.40f};
	staticInfo.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
			  physical_size, 2);

	int r = getFrameInfo(id, fd, s);
	ALOGDI("[%s] supported resolutions count:%d", __func__, r);
	if (!r) {
		ALOGE("sensor resolution value is invalid");
		return NULL;
	}
	/* pixel size for preview and still capture
	 * ACTIVE_ARRAY_SIZE should be similar with MAX_JPEG_SIZE
	 * */
	pixel_array_size[id][0] = s->frames[r-1].width;
	pixel_array_size[id][1] = s->frames[r-1].height;
	staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
			  pixel_array_size[id], 2);
	int32_t active_array_size[] = {
		0/*left*/, 0/*top*/,
		pixel_array_size[id][0]/*width*/,
		pixel_array_size[id][1]/*height*/
	};
	staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
			  active_array_size, 4);
	int32_t max_fps = checkMaxFps(r, &s->frames[0]);
	int32_t min_fps = checkMinFps(r, &s->frames[0]);
	/* For a device supports Limited Level
	 * available_fps_ranges is {min_fps, max_fps, max_fps, max_fps}
	 * min_fps <= 15 and max_fps = max frame rate
	 */
	int32_t record_fps = (min_fps + 5);
	int32_t available_fps_ranges[] = {
		min_fps, max_fps, record_fps, record_fps
	};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
			  available_fps_ranges,
			  sizeof(available_fps_ranges)/sizeof(int32_t));
	/* For CTS: android.hardware.camera2.cts.RecordingTest#testBasicRecording */
#if 0
	int64_t max = max_fps * 1e9;
#else
	int64_t max = min_fps * 1e9;
#endif
	staticInfo.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
			  &max, 1);

	/* AE Mode */
	/* For CTS : android.hardware.camera2.cts.BurstCaptureTest#testYuvBurst */
	uint8_t aeLockAvailable = ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
	staticInfo.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE,
			  &aeLockAvailable,
			  1);
	camera_metadata_rational exposureCompensationStep = {
		1/*numerator*/, 2/*denomiator*/
	};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
			  &exposureCompensationStep, 1);

	int32_t exposureCompensationRange[] = {-4, 4};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
			  exposureCompensationRange,
			  sizeof(exposureCompensationRange)/sizeof(int32_t));
         /*
	  *For CTS: android.hardware.camera2.cts.CaptureRequestTest#testFlashControl
          */
	int64_t exposureTimeRange[] = {-4, 4};
	staticInfo.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
			  exposureTimeRange,
			  sizeof(exposureTimeRange)/sizeof(int64_t));
	/* For CTS : android.hardware.camera2.cts.CaptureRequestTest#testAeModeAndLock */
	uint8_t avail_ae_modes[] = {
		ANDROID_CONTROL_AE_MODE_OFF,
		//ANDROID_CONTROL_AE_MODE_ON
	};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_MODES,
			  avail_ae_modes,
			  sizeof(avail_ae_modes)/sizeof(uint8_t));

	/* AWB Mode
	 * For CTS : android.hardware.camera2.cts.CameraDeviceTest#testCameraDevicePreviewTemplate
	 * if AWB lock is not supported, expect the control key to be non-exist or false
	 */
	uint8_t awbLockAvailable = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
	staticInfo.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
			  &awbLockAvailable, 1);

	uint8_t awbAvailableMode[] = {
		ANDROID_CONTROL_AWB_MODE_OFF,
		//ANDROID_CONTROL_AWB_MODE_AUTO
	};
	staticInfo.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
			  awbAvailableMode,
			  sizeof(awbAvailableMode)/sizeof(uint8_t));

	/* Available Control Mode */
	uint8_t available_control_modes[] = {
		ANDROID_CONTROL_MODE_OFF,
		//ANDROID_CONTROL_MODE_AUTO,
		//ANDROID_CONTROL_MODE_USE_SCENE_MODE
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_MODES,
			  available_control_modes,
			  sizeof(available_control_modes)/sizeof(uint8_t));

	/* AF Mode
	 */
	uint8_t available_af_modes[] = {
		ANDROID_CONTROL_AF_MODE_OFF,
		ANDROID_CONTROL_AF_MODE_AUTO,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO
	};
	staticInfo.update(ANDROID_CONTROL_AF_AVAILABLE_MODES,
			  available_af_modes,
			  sizeof(available_af_modes)/sizeof(uint8_t));

	int32_t max_latency = (!facing) ?
			ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL: MAX_SYNC_LATENCY;
	staticInfo.update(ANDROID_SYNC_MAX_LATENCY,
			  &max_latency, 1);

	/* TODO: handle variation of sensor */
	/* Format */
	int32_t scaler_formats[] = {
		HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, /* 0x22: same to Y/Cb/Cr 420 format */
		HAL_PIXEL_FORMAT_YV12, /* Y/Cr/Cb 420 format */
		HAL_PIXEL_FORMAT_YCbCr_420_888, /* 0x23: Y/Cb/Cr 420 format */
		HAL_PIXEL_FORMAT_BLOB /* 0x21: for jpeg */
	};
	int fmt_count = sizeof(scaler_formats)/sizeof(int32_t);
	staticInfo.update(ANDROID_SCALER_AVAILABLE_FORMATS,
			  scaler_formats, fmt_count);

	/* To support multi resolutions that the sensor don't support */
	int i, j = 0, count = 0;
#ifdef CAMERA_SUPPORT_SCALING
	int list_size = sizeof(supported_lists)/sizeof(struct v4l2_frame_info);
#else
	int list_size = 0;
#endif
	struct v4l2_frame_info lists[MAX_SUPPORTED_RESOLUTION];
	struct v4l2_frame_info *sensor_lists;
	bool crop = true;
	struct v4l2_frame_info crop_list = {0, s->crop.width, s->crop.height,
		{s->frames[0].interval[0], s->frames[0].interval[1]}};

	if (!s->crop.width || !s->crop.height)
		crop = false;

	if (crop) {
		sensor_lists = &crop_list;
		r = 1;
	} else
		sensor_lists = s->frames;

	for (i = 0; i < r; i++) {
#ifdef CAMERA_SUPPORT_SCALING
		for (; j < list_size; j++) {
			if ((supported_lists[j].width == sensor_lists[i].width) &&
					(supported_lists[j].height == sensor_lists[i].height)) {
				lists[count].index = count;
				lists[count].width = supported_lists[j].width;
				lists[count].height = supported_lists[j].height;
				lists[count].interval[0] = supported_lists[j].interval[0];
				lists[count].interval[1] = supported_lists[j].interval[1];
				j++;
				count++;
				break;
			} else if ((supported_lists[j].width * supported_lists[j].height) >
					(sensor_lists[i].width * sensor_lists[i].height)) {
				if ((sensor_lists[i].width / 32) == 0) {
					lists[count].index = count;
					lists[count].width = sensor_lists[i].width;
					lists[count].height = sensor_lists[i].height;
					lists[count].interval[0] = sensor_lists[i].interval[0];
					lists[count].interval[1] = sensor_lists[i].interval[1];
					count++;
				}
				break;
			} else if ((supported_lists[j].width * supported_lists[j].height) <
					(sensor_lists[i].width * sensor_lists[i].height)) {
				lists[count].index = count;
				lists[count].width = supported_lists[j].width;
				lists[count].height = supported_lists[j].height;
				lists[count].interval[0] = supported_lists[j].interval[0];
				lists[count].interval[1] = supported_lists[j].interval[1];
				count++;
			}
		}
#endif
		if (j == list_size) {
#ifdef CAMERA_SUPPORT_SCALING
			if ((sensor_lists[i].width / 32) == 0)
#endif
			{
				lists[count].index = count;
				lists[count].width = sensor_lists[i].width;
				lists[count].height = sensor_lists[i].height;
				lists[count].interval[0] = sensor_lists[i].interval[0];
				lists[count].interval[1] = sensor_lists[i].interval[1];
				count++;
			}
		}
	}

#ifdef CAMERA_SUPPORT_SCALING
	while (j < list_size) {
		lists[count].index = count;
		lists[count].width = supported_lists[j].width;
		lists[count].height = supported_lists[j].height;
		lists[count].interval[0] = supported_lists[j].interval[0];
		lists[count].interval[1] = supported_lists[j].interval[1];
		count++;
		j++;
	}
#endif
	for (i = 0; i < count; i++)
		ALOGDI("[DEBUG:%d] width:%d, height:%d, min:%d, max:%d", i,
				lists[i].width, lists[i].height,
				lists[i].interval[0], lists[i].interval[1]);

	/* TODO: handle variation of sensor */
	/* check whether same format has serveral resolutions */
	int array_size = fmt_count * 4 * count;
	int32_t available_stream_configs[array_size];
	int64_t available_frame_min_durations[array_size];
	for(int f = 0; f < fmt_count; f++) {
		for (int j = 0; j < count ; j++) {
			int offset = f*4*count + j*4;
			ALOGDV("[%s] f:%d, j:%d, r:%d, offset:%d", __func__, f, j, count, offset);
			available_stream_configs[offset] = scaler_formats[f];
			available_stream_configs[1+offset] = lists[j].width;
			available_stream_configs[2+offset] = lists[j].height;
			available_stream_configs[3+offset] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
			available_frame_min_durations[offset] = scaler_formats[f];
			available_frame_min_durations[1+offset] = lists[j].width;
			available_frame_min_durations[2+offset] = lists[j].height;
			available_frame_min_durations[3+offset] = lists[j].interval[V4L2_INTERVAL_MIN];
		}
	}
	staticInfo.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
			  available_stream_configs,
			  array_size);
	staticInfo.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
			  available_frame_min_durations,
			  array_size);

	int32_t stall_formats[] = {
		HAL_PIXEL_FORMAT_BLOB /* 0x21: for jpeg */
	};
	fmt_count = sizeof(stall_formats)/sizeof(int32_t);
	int stall_array_size = fmt_count * 4 * count;
	int64_t available_stall_durations[stall_array_size];
	for (int f = 0; f < fmt_count; f++) {
		for (int j = 0; j < count; j++) {
			int offset = f*4*count + j*4;
			available_stall_durations[f+offset] = stall_formats[f];
			available_stall_durations[f+1+offset] = lists[j].width;
			available_stall_durations[f+2+offset] = lists[j].height;
			available_stall_durations[f+3+offset] = available_fps_ranges[0];
		}
	}
	staticInfo.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
			  available_stall_durations,
			  stall_array_size);

	int32_t available_thumbnail_sizes[] = {
		0, 0, /* width, height */
		144, 96,
		160, 90,
		160, 120,
		176, 120,
		160, 160,
	};

	staticInfo.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
			  available_thumbnail_sizes,
			  sizeof(available_thumbnail_sizes)/sizeof(int32_t));

	/* TODO: 10M is too big... */
	/*
	 * size = width * height for BLOB format * scaling factor
	 */
	int32_t jpegMaxSize = checkMaxJpegSize(r, lists);
	staticInfo.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

	uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
	staticInfo.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
			  &timestampSource, 1);

	int32_t available_high_speed_video_config[] = {};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
			  available_high_speed_video_config, 0);

	/* Effect Mode */
	uint8_t avail_effects[] = {
		ANDROID_CONTROL_EFFECT_MODE_OFF,
		ANDROID_CONTROL_EFFECT_MODE_MONO,
		ANDROID_CONTROL_EFFECT_MODE_NEGATIVE,
		ANDROID_CONTROL_EFFECT_MODE_SEPIA,
		ANDROID_CONTROL_EFFECT_MODE_AQUA
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_EFFECTS,
			  avail_effects,sizeof(avail_effects));

	/* Scene Mode */
	uint8_t avail_scene_modes[] = {
		ANDROID_CONTROL_SCENE_MODE_PORTRAIT,
		ANDROID_CONTROL_SCENE_MODE_LANDSCAPE,
		ANDROID_CONTROL_SCENE_MODE_SPORTS,
		/* if FACE_DETECTION mode is supported, following modes must be supported */
		ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
			  avail_scene_modes,
			  sizeof(avail_scene_modes));

	uint8_t scene_mode_overrides[] = {
		// ANDROID_CONTROL_SCENE_MODE_PORTRAIT
		ANDROID_CONTROL_AE_MODE_OFF,
		ANDROID_CONTROL_AWB_MODE_OFF,
		ANDROID_CONTROL_AF_MODE_AUTO,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		// ANDROID_CONTROL_SCENE_MODE_LANDSCAPE
		ANDROID_CONTROL_AE_MODE_OFF,
		ANDROID_CONTROL_AWB_MODE_OFF,
		ANDROID_CONTROL_AF_MODE_AUTO,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		// ANDROID_CONTROL_SCENE_MODE_SPORTS
		ANDROID_CONTROL_AE_MODE_OFF,
		ANDROID_CONTROL_AWB_MODE_OFF,
		ANDROID_CONTROL_AF_MODE_AUTO,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		// ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY
		ANDROID_CONTROL_AE_MODE_OFF,
		ANDROID_CONTROL_AWB_MODE_OFF,
		ANDROID_CONTROL_AF_MODE_AUTO,
		//ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
	};
	staticInfo.update(ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
			  scene_mode_overrides,
			  sizeof(scene_mode_overrides));
	/* End Scene Mode */
	/* TODO: handle antibanding */
	/* Antibanding Mode
	 * For CTS: android.hardware.camera2.cts.CameraDeviceTest#testCameraDevicePreviewTemplate
         * and android.hardware.camera2.cts.CameraDeviceTest#testCameraDeviceRecordingTemplate
	 * all values that setted for each template should be setted for
         * ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES too
         */
	uint8_t avail_antibanding_modes[] = {
		ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,
		ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
			  avail_antibanding_modes,
			  sizeof(avail_antibanding_modes)/sizeof(uint8_t));
	uint8_t avail_abberation_modes[] = {
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF,
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST,
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY
	};
	staticInfo.update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
			  avail_abberation_modes,
			  sizeof(avail_abberation_modes)/sizeof(uint8_t));
	/* End Antibanding Mode */

	int32_t max3aRegions[3] = {/*AE*/0,/*AWB*/0,/*AF*/0};
	staticInfo.update(ANDROID_CONTROL_MAX_REGIONS,
			  max3aRegions, 3);
	/* Flash Mode */
	uint8_t flashAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
	staticInfo.update(ANDROID_FLASH_INFO_AVAILABLE, &flashAvailable, 1);

	/*
	 * Lens Shanding Mode
	 * all metadata that related LSC mode and map should be supported for CTS
	 */
	uint8_t available_lens_shading_map_modes[] = {
		ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF,
		/*,ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON*/
	};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
					  available_lens_shading_map_modes,
					  sizeof(available_lens_shading_map_modes));

	uint8_t available_shading_modes[] = {
		ANDROID_SHADING_MODE_OFF,
		/*ANDROID_SHADING_MODE_HIGH_QUALITY*/
	};
	staticInfo.update(ANDROID_SHADING_AVAILABLE_MODES,
					  available_shading_modes,
					  sizeof(available_shading_modes));
	int32_t shading_map[] = {1, 1};
	staticInfo.update(ANDROID_LENS_INFO_SHADING_MAP_SIZE,
			  shading_map, sizeof(shading_map)/sizeof(int32_t));

	/* End Lens Shanding Mode */

	uint8_t max_pipeline_depth = MAX_BUFFER_COUNT;
	staticInfo.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
					  &max_pipeline_depth, 1);

	/*For CTS:android.hardware.camera2.cts.ExtendedCameraCharacteristicsTest#testKeys */
	int32_t partial_result_count = 1;
	staticInfo.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
			  &partial_result_count,
			  1);

	uint8_t availableVstabModes[] = {
		ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF,
		ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_ON
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
			  availableVstabModes, sizeof(availableVstabModes));

	uint8_t stabilization_mode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_ON;
        staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                          &stabilization_mode, 1);

	float min_focus_dis = 0.1f;
	staticInfo.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                          &min_focus_dis, 1);
	staticInfo.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
			  &min_focus_dis, 1);

	uint8_t available_noise_red_modes[] = {
		ANDROID_NOISE_REDUCTION_MODE_OFF,
		//ANDROID_NOISE_REDUCTION_MODE_FAST,
		//ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY
	};
	staticInfo.update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
			  available_noise_red_modes,
			  sizeof(&available_noise_red_modes)/sizeof(uint8_t));

	int32_t avail_testpattern_modes[2] = {
		ANDROID_SENSOR_TEST_PATTERN_MODE_OFF,
		ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR};
	staticInfo.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
			  avail_testpattern_modes, 2);

	int32_t max_output_streams[3] = {
		1 /*MAX_STALLING_STREAMS*/,
		3 /*MAX_PROCESSED_STREAMS*/,
		1 /*MAX_RAW_STREAMS*/};
	staticInfo.update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                          max_output_streams, 3);

	int32_t max_input_streams = 0;
	staticInfo.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
			  &max_input_streams, 1);

	int32_t max_stall_duration = 1/*MAX_REPROCESS_STALL*/;
	staticInfo.update(ANDROID_REPROCESS_MAX_CAPTURE_STALL,
                          &max_stall_duration, 1);

	uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_FREEFORM;
	staticInfo.update(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);

	uint8_t available_capabilities[8];
	uint8_t available_capabilities_count = 0;
	available_capabilities[available_capabilities_count++] =
		ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
	if (facing)
		available_capabilities[available_capabilities_count++] =
			ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW;

	staticInfo.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
			  available_capabilities,
			  available_capabilities_count);

	/* Supported request meta data key arrays
	 * each metadata int the list should be setted fot the metadata
	 * that return to a framework by constructDefaultRequestSettings
	 */
	int32_t request_keys_basic[] = {
		ANDROID_REQUEST_TYPE,
		ANDROID_REQUEST_ID,
		ANDROID_CONTROL_CAPTURE_INTENT,
		ANDROID_CONTROL_AF_TRIGGER,
		ANDROID_CONTROL_AE_LOCK,
		ANDROID_CONTROL_AE_MODE,
		ANDROID_CONTROL_AE_ANTIBANDING_MODE,
		ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
		ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
		ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
		ANDROID_CONTROL_AF_MODE,
		ANDROID_CONTROL_AWB_MODE,
		ANDROID_CONTROL_AWB_LOCK,
		ANDROID_CONTROL_EFFECT_MODE,
		ANDROID_CONTROL_MODE,
		ANDROID_CONTROL_SCENE_MODE,
		ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
		ANDROID_FLASH_MODE,
		ANDROID_JPEG_ORIENTATION,
		ANDROID_JPEG_QUALITY,
		ANDROID_JPEG_THUMBNAIL_QUALITY,
		ANDROID_JPEG_THUMBNAIL_SIZE,
		ANDROID_JPEG_GPS_COORDINATES,
		ANDROID_JPEG_GPS_PROCESSING_METHOD,
		ANDROID_JPEG_GPS_TIMESTAMP,
		ANDROID_SCALER_CROP_REGION,
		ANDROID_STATISTICS_FACE_DETECT_MODE,
		ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
		ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
		//ANDROID_COLOR_CORRECTION_MODE,
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
		//ANDROID_COLOR_CORRECTION_GAINS,
		//ANDROID_COLOR_CORRECTION_TRANSFORM,
		ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
		ANDROID_SENSOR_TEST_PATTERN_MODE,
	};
	size_t request_keys_cnt =
		sizeof(request_keys_basic)/sizeof(request_keys_basic[0]);
	//NOTE: Please increase available_request_keys array size before
	//adding any new entries.
	int32_t available_request_keys[request_keys_cnt+1];
	memcpy(available_request_keys, request_keys_basic,
		   sizeof(request_keys_basic));
	//NOTE: Please increase available_request_keys array size before
	//adding any new entries.
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
					  available_request_keys, request_keys_cnt);
	/* Supported result meta data key arrays */
	int32_t result_keys_basic[] = {
		//ANDROID_COLOR_CORRECTION_MODE,
		//ANDROID_COLOR_CORRECTION_TRANSFORM,
		//ANDROID_COLOR_CORRECTION_GAINS,
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
		ANDROID_CONTROL_AE_ANTIBANDING_MODE,
		ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
		ANDROID_CONTROL_AE_LOCK,
		//ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
		ANDROID_CONTROL_AE_STATE,
		ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
		ANDROID_CONTROL_AF_MODE,
        	ANDROID_CONTROL_AF_STATE,
        	ANDROID_CONTROL_AF_TRIGGER,
		ANDROID_CONTROL_AWB_LOCK,
		ANDROID_CONTROL_AWB_MODE,
		ANDROID_CONTROL_AWB_STATE,
		ANDROID_CONTROL_CAPTURE_INTENT,
		ANDROID_CONTROL_EFFECT_MODE,
		ANDROID_CONTROL_MODE,
		ANDROID_CONTROL_SCENE_MODE,
		ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
		ANDROID_FLASH_MODE,
		ANDROID_FLASH_STATE,
		ANDROID_LENS_FOCAL_LENGTH,
		ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
		ANDROID_SCALER_CROP_REGION,
		ANDROID_STATISTICS_FACE_DETECT_MODE,
		//ANDROID_STATISTICS_SCENE_FLICKER,
		ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
        	ANDROID_SENSOR_TIMESTAMP,
		ANDROID_JPEG_ORIENTATION,
		ANDROID_JPEG_QUALITY,
		ANDROID_JPEG_THUMBNAIL_QUALITY,
		ANDROID_JPEG_THUMBNAIL_SIZE,
		ANDROID_JPEG_GPS_COORDINATES,
		ANDROID_JPEG_GPS_PROCESSING_METHOD,
		ANDROID_JPEG_GPS_TIMESTAMP,
	};
	size_t result_keys_cnt =
	    sizeof(result_keys_basic)/sizeof(result_keys_basic[0]);
	//NOTE: Please increase available_result_keys array size before
	//adding any new entries.
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
					  result_keys_basic, result_keys_cnt);

	/*For CTS:android.hardware.camera2.cts.ExtendedCameraCharacteristicsTest#testKeys */
	/* following metadatas should be setted for static metadta */
	int32_t available_characteristics_keys[] = {
		ANDROID_CONTROL_AE_LOCK_AVAILABLE,
		ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
		ANDROID_CONTROL_AVAILABLE_MODES,
		ANDROID_SHADING_AVAILABLE_MODES,
		ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
		ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
		ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
		ANDROID_CONTROL_AE_AVAILABLE_MODES,
		ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
		ANDROID_CONTROL_AE_COMPENSATION_RANGE,
		ANDROID_CONTROL_AE_COMPENSATION_STEP,
		ANDROID_CONTROL_AF_AVAILABLE_MODES,
		ANDROID_CONTROL_AVAILABLE_EFFECTS,
		ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
		ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
		ANDROID_CONTROL_AWB_AVAILABLE_MODES,
		ANDROID_FLASH_INFO_AVAILABLE,
		ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
		ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
		ANDROID_JPEG_MAX_SIZE,
		//ANDROID_JPEG_THUMBNAIL_SIZE,
		ANDROID_LENS_FACING,
		ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
		ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
		ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
		ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
		ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
		ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
		ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
		ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
		ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
		ANDROID_SCALER_CROPPING_TYPE,
		ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
		ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
		ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
		ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
		ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
		ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
		ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
		ANDROID_SENSOR_ORIENTATION,
		ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
		ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
		ANDROID_SYNC_MAX_LATENCY,
		ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
		ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
		ANDROID_REPROCESS_MAX_CAPTURE_STALL,
	};
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
			  available_characteristics_keys,
			  sizeof(available_characteristics_keys)/sizeof(int32_t));

	ALOGDI("End initStaticMetadata");
	return staticInfo.release();
}

camera_metadata_t* translateMetadata (uint32_t id, const camera_metadata_t *request,
		exif_attribute_t *exif,
		nsecs_t timestamp,
		uint8_t pipeline_depth)
{
	if (request == NULL) {
		ALOGDI("[%s] No Metadata", __func__);
		return NULL;
	}
	CameraMetadata meta;
	CameraMetadata metaData;

	meta = request;

	ALOGDV("[%s] Exif:%s, timestamp:%ld, pipeline:%d", __func__,
			(exif) ? "exist" : "null", timestamp, pipeline_depth);

	if (meta.exists(ANDROID_REQUEST_ID)) {
		int32_t request_id = meta.find(ANDROID_REQUEST_ID).data.i32[0];
		metaData.update(ANDROID_REQUEST_ID, &request_id, 1);
	}
	metaData.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);
	metaData.update(ANDROID_REQUEST_PIPELINE_DEPTH, &pipeline_depth, 1);

	if (meta.exists(ANDROID_CONTROL_CAPTURE_INTENT)) {
		uint8_t capture_intent =
			meta.find(ANDROID_CONTROL_CAPTURE_INTENT).data.u8[0];
		ALOGDV("capture_intent:%d", capture_intent);
		metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);
	}

	if (meta.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
		int32_t fps_range[2];
		fps_range[0] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
		fps_range[1] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[1];
		ALOGDV("ANDROID_CONTROL_AE_TARGET_FPS_RANGE-min:%d,max:%d",
				fps_range[0], fps_range[1]);
		metaData.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, fps_range, 2);
	}
	if (meta.exists(ANDROID_CONTROL_AF_TRIGGER) &&
	meta.exists(ANDROID_CONTROL_AF_TRIGGER_ID)) {
		uint8_t trigger = meta.find(ANDROID_CONTROL_AF_TRIGGER).data.u8[0];
		int32_t trigger_id = meta.find(ANDROID_CONTROL_AF_TRIGGER_ID).data.i32[0];
		uint8_t afState;

		metaData.update(ANDROID_CONTROL_AF_TRIGGER, &trigger, 1);
		if (trigger == ANDROID_CONTROL_AF_TRIGGER_START) {
			ALOGDV("AF_TRIGGER_START");
			afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		} else if (trigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
			ALOGDV("AF_TRIGGER_CANCELL");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		} else {
			ALOGDV("AF_TRIGGER_IDLE");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		}
		ALOGDV("ANDROID_CONTROL_AF_STATE:%d", afState);
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AF_MODE)) {
		uint8_t afMode = meta.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
		uint8_t afState;
		ALOGDV("ANDROID_CONTROL_AF_MODE:%d", afMode);
		if ((afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE) ||
				(afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO))
			afMode = ANDROID_CONTROL_AF_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);
		if (afMode == ANDROID_CONTROL_AF_MODE_OFF)
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		else
			afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
		ALOGDV("ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION");
		int32_t expCompensation =
			meta.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
		metaData.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &expCompensation, 1);
	}
	if (meta.exists(ANDROID_CONTROL_MODE)) {
		uint8_t metaMode = meta.find(ANDROID_CONTROL_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_MODE:%d", metaMode);
		metaData.update(ANDROID_CONTROL_MODE, &metaMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_LOCK)) {
		uint8_t aeLock = meta.find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
		ALOGDV("ANDROID_CONTROL_AE_LOCK:%d", aeLock);
		metaData.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_MODE)) {
		uint8_t aeMode = meta.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
		uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
		//uint8_t aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
		ALOGDV("ANDROID_CONTROL_AE_MODE:%d", aeMode);
		if (aeMode != ANDROID_CONTROL_AE_MODE_OFF)
			aeMode = ANDROID_CONTROL_AE_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
		metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
		uint8_t aeAntiBandingMode = meta.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_AE_ANTIBANDING_MODE:%d", aeAntiBandingMode);
		metaData.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntiBandingMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER) &&
		meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_ID)) {
		uint8_t trigger = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER).data.u8[0];
		uint8_t trigger_id = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_ID).data.u8[0];
		uint8_t aeState;

		ALOGDV("ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER:%d, ID:%d", trigger, trigger_id);
		metaData.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &trigger, 1);
		if (trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
			aeState = ANDROID_CONTROL_AE_STATE_LOCKED;
			//uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
			//metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		} else {
			aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
			metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		}
		ALOGDV("ANDROID_CONTROL_AE_STATE:%d", aeState);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_LOCK)) {
		uint8_t awbLock =
			meta.find(ANDROID_CONTROL_AWB_LOCK).data.u8[0];
		ALOGDV("ANDROID_CONTROL_AWB_LOCK:%d", awbLock);
		metaData.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_MODE)) {
		uint8_t awbMode =
			meta.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_AWB_MODE:%d", awbMode);
		if (awbMode != ANDROID_CONTROL_AWB_MODE_OFF)
			awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
		uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
		//uint8_t awbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
		ALOGDV("ANDROID_CONTROL_AWB_STATE:%d", awbState);
		metaData.update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);
		if (exif)
			exif->setWhiteBalance(awbMode);
	}
	if (meta.exists(ANDROID_CONTROL_SCENE_MODE)) {
		uint8_t sceneMode = meta.find(ANDROID_CONTROL_SCENE_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_SCENE_MODE:%d", sceneMode);
		metaData.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);
		if (exif)
			exif->setSceneCaptureType(sceneMode);
	}
	/*
	if (meta.exists(ANDROID_COLOR_CORRECTION_MODE)) {
	uint8_t colorCorrectMode =
	meta.find(ANDROID_COLOR_CORRECTION_MODE).data.u8[0];
	ALOGDI("ANDROID_COLOR_CORRECTION_MODE:%d", colorCorrectMode);
	metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectMode, 1);
	}
	*/
	if (meta.exists(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
		uint8_t colorCorrectAbeMode =
			meta.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE).data.u8[0];
		ALOGDV("ANDROID_COLOR_CORRECTION_ABERRATION_MODE:%d", colorCorrectAbeMode);
		metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectAbeMode, 1);
	}
	if (meta.exists(ANDROID_FLASH_MODE)) {
		uint8_t flashMode = meta.find(ANDROID_FLASH_MODE).data.u8[0];
		uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
		ALOGDV("ANDROID_FLASH_MODE:%d", flashMode);
		if (flashMode != ANDROID_FLASH_MODE_OFF)
			flashState = ANDROID_FLASH_STATE_READY;
		ALOGDV("ANDROID_FLASH_STATE:%d", flashState);
		metaData.update(ANDROID_FLASH_STATE, &flashState, 1);
		metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);
		if (exif)
			exif->setFlashMode(flashMode);
	}
	/*
	if (meta.exists(ANDROID_EDGE_MODE)) {
		uint8_t edgeMode = meta.find(ANDROID_EDGE_MODE).data.u8[0];
		ALOGDI("ANDROID_EDGE_MODE:%d", edgeMode);
		metaData.update(ANDROID_EDGE_MODE, &edgeMode, 1);
	}
	if (meta.exists(ANDROID_HOT_PIXEL_MODE)) {
		uint8_t hotPixelMode =
		meta.find(ANDROID_HOT_PIXEL_MODE).data.u8[0];
		ALOGDI("ANDROID_HOT_PIXEL_MODE:%d", hotPixelMode);
		metaData.update(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
	}
	*/
	if (meta.exists(ANDROID_LENS_FOCAL_LENGTH)) {
		float focalLength =
			meta.find(ANDROID_LENS_FOCAL_LENGTH).data.f[0];
		ALOGDV("ANDROID_LENS_FOCAL_LENGTH:%f", focalLength);
		metaData.update(ANDROID_LENS_FOCAL_LENGTH, &focalLength, 1);
		rational_t focal_length;
		focal_length.num = (uint32_t)(focalLength * EXIF_DEF_FOCAL_LEN_DEN);
		focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
		if (exif)
			exif->setFocalLength(focal_length);
	}
	if (meta.exists(ANDROID_LENS_OPTICAL_STABILIZATION_MODE)) {
		uint8_t optStabMode =
			meta.find(ANDROID_LENS_OPTICAL_STABILIZATION_MODE).data.u8[0];
		ALOGDV("ANDROID_LENS_OPTICAL_STABILIZATION_MODE:%d", optStabMode);
		metaData.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE)) {
		uint8_t vsMode = meta.find(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_VIDEO_STABILIZATION_MODE:%d", vsMode);
		metaData.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vsMode, 1);
	}
	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		int32_t scalerCropRegion[4];
		bool scaling = true;
		scalerCropRegion[0] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		scalerCropRegion[1] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		scalerCropRegion[2] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		scalerCropRegion[3] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		ALOGDV("ANDROID_SCALER_CROP_REGION:left-%d,top-%d,width-%d,height-%d",
				scalerCropRegion[0], scalerCropRegion[1], scalerCropRegion[2],
				scalerCropRegion[3]);
		metaData.update(ANDROID_SCALER_CROP_REGION, scalerCropRegion, 4);
		if (((scalerCropRegion[2] - scalerCropRegion[0]) == pixel_array_size[id][0]) &&
				((scalerCropRegion[3] - scalerCropRegion[1]) == pixel_array_size[id][1]))
				scaling = false;
		if (scaling && exif)
			exif->setCropResolution(scalerCropRegion[0], scalerCropRegion[1],
					scalerCropRegion[2], scalerCropRegion[3]);
		else if (exif)
			exif->setCropResolution(0, 0, 0, 0);
	} else {
		if (exif)
			exif->setCropResolution(0, 0, 0, 0);
	}

	if (meta.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
		int64_t sensorExpTime =
			meta.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
		ALOGDV("ANDROID_SENSOR_EXPOSURE_TIME:%ld", sensorExpTime);
		metaData.update(ANDROID_SENSOR_EXPOSURE_TIME, &sensorExpTime, 1);
		if (exif)
			exif->setExposureTime(sensorExpTime);
	}
	if (meta.exists(ANDROID_SENSOR_FRAME_DURATION)) {
		int64_t sensorFrameDuration =
			meta.find(ANDROID_SENSOR_FRAME_DURATION).data.i64[0];
		int64_t minFrameDuration = 0;
		if (meta.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
			minFrameDuration = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
			ALOGDI("minFrame:%lld", minFrameDuration);
			minFrameDuration = (long) 1e9/ minFrameDuration;
		}
		ALOGDV("ANDROID_SENSOR_FRAME_DURATION:%ld, Min:%lld",
				sensorFrameDuration, minFrameDuration);
		if (sensorFrameDuration < minFrameDuration)
			sensorFrameDuration = minFrameDuration;
		metaData.update(ANDROID_SENSOR_FRAME_DURATION, &sensorFrameDuration, 1);
	}
	/*
	if (meta.exists(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW)) {
		int64_t sensorRollingShutterSkew =
		meta.find(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW).data.i64[0];
		ALOGDI("ANDROID_SENSOR_ROLLING_SHUTTER_SKEW:%ld", sensorRollingShutterSkew);
		metaData.update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &sensorRollingShutterSkew, 1);
	}
	*/
	if (meta.exists(ANDROID_SHADING_MODE)) {
		uint8_t  shadingMode =
			meta.find(ANDROID_SHADING_MODE).data.u8[0];
		ALOGDV("ANDROID_SHADING_MODE:%d", shadingMode);
		metaData.update(ANDROID_SHADING_MODE, &shadingMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_FACE_DETECT_MODE)) {
		uint8_t fwk_facedetectMode =
			meta.find(ANDROID_STATISTICS_FACE_DETECT_MODE).data.u8[0];
		ALOGDV("ANDROID_STATISTICS_FACE_DETECT_MODE:%d", fwk_facedetectMode);
		metaData.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &fwk_facedetectMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP)) {
		uint8_t sharpnessMapMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP).data.u8[0];
		ALOGDV("ANDROID_STATISTICS_LENS_SHADING_MAP:%d", sharpnessMapMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP, &sharpnessMapMode, 1);
	}
#if 0
	if (meta.exists(ANDROID_COLOR_CORRECTION_GAINS)) {
		float colorCorrectGains[4];
		colorCorrectGains[0] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[0];
		colorCorrectGains[1] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[1];
		colorCorrectGains[2] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[2];
		colorCorrectGains[3] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[3];
		ALOGDI("ANDROID_COLOR_CORRECTION_GAINS-ColorGain:%f,%f,%f,%f",
				colorCorrectGains[0], colorCorrectGains[1], colorCorrectGains[2],
				colorCorrectGains[3]);
		metaData.update(ANDROID_COLOR_CORRECTION_GAINS, colorCorrectGains, 4);
	}
	if (meta.exists(ANDROID_COLOR_CORRECTION_TRANSFORM)) {
		camera_metadata_rational_t ccTransform[9];
		ccTransform[0].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[0].numerator;
		ccTransform[0].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[0].denominator;
		ccTransform[1].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[1].numerator;
		ccTransform[1].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[1].denominator;
		ccTransform[2].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[2].numerator;
		ccTransform[2].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[2].denominator;
		ccTransform[3].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[3].numerator;
		ccTransform[3].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[3].denominator;
		ccTransform[4].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[4].numerator;
		ccTransform[4].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[4].denominator;
		ccTransform[5].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[5].numerator;
		ccTransform[5].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[5].denominator;
		ccTransform[6].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[6].numerator;
		ccTransform[6].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[6].denominator;
		ccTransform[7].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[7].numerator;
		ccTransform[7].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[7].denominator;
		ccTransform[8].numerator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[8].numerator;
		ccTransform[8].denominator = meta.find(ANDROID_COLOR_CORRECTION_TRANSFORM).data.r[8].denominator;
		ALOGDI("ANDROID_COLOR_CORRECTION_TRANSFORM:%d/%d, %d/%d, %d/%d",
		ccTransform[0].numerator/ccTransform[0].denominator,
		ccTransform[1].numerator/ccTransform[1].denominator,
		ccTransform[2].numerator/ccTransform[2].denominator,
		ccTransform[3].numerator/ccTransform[0].denominator,
		ccTransform[4].numerator/ccTransform[1].denominator,
		ccTransform[5].numerator/ccTransform[2].denominator,
		ccTransform[6].numerator/ccTransform[0].denominator,
		ccTransform[7].numerator/ccTransform[1].denominator,
		ccTransform[8].numerator/ccTransform[2].denominator);
		metaData.update(ANDROID_COLOR_CORRECTION_TRANSFORM, ccTransform, 9);
	}
#endif
	if (meta.exists(ANDROID_CONTROL_EFFECT_MODE)) {
		uint8_t fwk_effectMode =
			meta.find(ANDROID_CONTROL_EFFECT_MODE).data.u8[0];
		ALOGDV("ANDROID_CONTROL_EFFECT_MODE:%d", fwk_effectMode);
		metaData.update(ANDROID_CONTROL_EFFECT_MODE, &fwk_effectMode, 1);
	}
	if (meta.exists(ANDROID_SENSOR_TEST_PATTERN_MODE)) {
		int32_t fwk_testPatternMode =
			meta.find(ANDROID_SENSOR_TEST_PATTERN_MODE).data.i32[0];
		ALOGDV("ANDROID_SENSOR_TEST_PATTERN_MODE:%d", fwk_testPatternMode);
		metaData.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &fwk_testPatternMode, 1);
	}
	if (meta.exists(ANDROID_JPEG_ORIENTATION)) {
		int32_t orientation =
			meta.find(ANDROID_JPEG_ORIENTATION).data.i32[0];
		ALOGDV("ANDROID_JPEG_ORIENTATION:%d", orientation);
		metaData.update(ANDROID_JPEG_ORIENTATION, &orientation, 1);
		int32_t exifOri;
		if (orientation == 90)
			exifOri = EXIF_ORIENTATION_90;
		else if(orientation == 180)
			exifOri = EXIF_ORIENTATION_180;
		else if(orientation == 270)
			exifOri = EXIF_ORIENTATION_270;
		else
			exifOri = EXIF_ORIENTATION_UP;
		if (exif)
			exif->setOrientation(exifOri);
	}
	if (meta.exists(ANDROID_JPEG_QUALITY)) {
		uint8_t quality =
			meta.find(ANDROID_JPEG_QUALITY).data.u8[0];
		ALOGDV("ANDROID_JPEG_QUALITY:%d", quality);
		metaData.update(ANDROID_JPEG_QUALITY, &quality, 1);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
		uint8_t thumb_quality =
			meta.find(ANDROID_JPEG_THUMBNAIL_QUALITY).data.u8[0];
		ALOGDV("ANDROID_JPEG_THUMBNAIL_QUALITY:%d", thumb_quality);
		metaData.update(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumb_quality, 1);
		if (exif)
			exif->setThumbnailQuality(thumb_quality);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
		int32_t size[2];
		size[0] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[0];
		size[1] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[1];
		ALOGDV("ANDROID_JPEG_THUMBNAIL_SIZE- width:%d, height:%d",
				size[0], size[1]);
		metaData.update(ANDROID_JPEG_THUMBNAIL_SIZE, size, 2);
		if (exif)
			exif->setThumbResolution(size[0], size[1]);
	} else {
		if (exif)
			exif->setThumbResolution(0, 0);
	}

	if (meta.exists(ANDROID_JPEG_GPS_COORDINATES)) {
		double gps[3];
		gps[0] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[0];
		gps[1] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[1];
		gps[2] = meta.find(ANDROID_JPEG_GPS_COORDINATES).data.d[2];
		ALOGDV("ANDROID_JPEG_GPS_COORDINATES-%f:%f:%f", gps[0], gps[1], gps[2]);
		if (exif)
			exif->setGpsCoordinates(gps);
		metaData.update(ANDROID_JPEG_GPS_COORDINATES, gps, 3);
	} else {
		double gps[3];
		memset(gps, 0x0, sizeof(double) * 3);
		if (exif)
			exif->setGpsCoordinates(gps);
	}

	if (meta.exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
		ALOGDV("ANDROID_JPEG_GPS_PROCESSING_METHOD count:%d",
				meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
		if (exif)
			exif->setGpsProcessingMethod(meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8,
					meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
		metaData.update(ANDROID_JPEG_GPS_PROCESSING_METHOD,
				meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).data.u8,
				meta.find(ANDROID_JPEG_GPS_PROCESSING_METHOD).count);
	}

	if (meta.exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
		int64_t timestamp = meta.find(ANDROID_JPEG_GPS_TIMESTAMP).data.i64[0];
		ALOGDV("ANDROID_JPEG_GPS_TIMESTAMP:%lld", timestamp);
		if (exif)
			exif->setGpsTimestamp(timestamp);
		metaData.update(ANDROID_JPEG_GPS_TIMESTAMP, &timestamp, 1);
	}

	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
		uint8_t shadingMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE).data.u8[0];
		ALOGDV("ANDROID_STATISTICS_LENS_SHADING_MAP_MODE:%d", shadingMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingMode, 1);
	}
	/*
	if (meta.exists(ANDROID_STATISTICS_SCENE_FLICKER)) {
		uint8_t sceneFlicker =
		meta.find(ANDROID_STATISTICS_SCENE_FLICKER).data.u8[0];
		ALOGDI("ANDROID_STATISTICS_SCENE_FLICKER:%d", sceneFlicker);
		metaData.update(ANDROID_STATISTICS_SCENE_FLICKER, &sceneFlicker, 1);
	}
	*/
	meta.clear();
	return metaData.release();
}

}; /*namespace android*/
