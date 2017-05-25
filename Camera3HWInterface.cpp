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

/*****************************************************************************/
/* Camera3 Hal Interface Class						     */
/*****************************************************************************/
const camera_metadata_t *Camera3HWInterface::initStaticMetadata(int camera_id)
{
	CameraMetadata staticInfo;
	const camera_metadata_t *meta;

	ALOGE("[%s] cameraID:%d\n", __FUNCTION__, camera_id);
	uint8_t facingBack = camera_id ? ANDROID_LENS_FACING_FRONT :
		ANDROID_LENS_FACING_BACK;

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

	/*
	 * TW9900
	 * FocalLength = 3.43f
	 * Aperture = 2.7f
	 * MinFocusDistance = 0.1f
	 * FNumber = 2.7f
	 * MaxFaceCount = 1
	 * Resoultion width = 704, height = 480
	 * Avaliable FpsRanges 15, 30
	 * Exposure Min -3, Max 3
	 */
	float focal_lengths = 3.43f;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
			  &focal_lengths, 1);

	float apertures = 2.7f;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
			  &apertures, 1);

	float filter_densities = 0;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
			  &filter_densities, 1);

	float min_focus_dis = 0.1f;
	staticInfo.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
			  &min_focus_dis, 1);
	staticInfo.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
			  &min_focus_dis, 1);

	uint8_t stabilization_mode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
			  &stabilization_mode, 1);
	int32_t shading_map[] = {1, 1};
	staticInfo.update(ANDROID_LENS_INFO_SHADING_MAP_SIZE,
			  shading_map, sizeof(shading_map)/sizeof(int32_t));

	float physical_size[2] = {3.20f, 2.40f};
	staticInfo.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
			  physical_size, 2);

	int64_t exposure_time[2] = {-3, 3};
	staticInfo.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
			  exposure_time, 2);

	int64_t f_duration [] = {33331760L, 30000000000L};
	staticInfo.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
			  &f_duration[1], 1);

	int64_t frame_duration = default_frame_duration;
	staticInfo.update(ANDROID_SENSOR_FRAME_DURATION, &frame_duration, 1);

	uint8_t filter_arrangement = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;
	staticInfo.update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
			  &filter_arrangement, 1);

	int32_t pixel_array_size[2] = {704, 480};
	staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
			  pixel_array_size, 2);

	int32_t active_array_size[] = {0, 0, pixel_array_size[0], pixel_array_size[1]};
	staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
			  active_array_size, 4);

	int32_t white_level = 4000;
	staticInfo.update(ANDROID_SENSOR_INFO_WHITE_LEVEL,
			  &white_level, 1);

	int32_t black_level_pattern[4] = {1000, 1000, 1000, 1000};
	staticInfo.update(ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
			  black_level_pattern, 4);

	int64_t flash_charge_duration = 0;
	staticInfo.update(ANDROID_FLASH_INFO_CHARGE_DURATION,
			  &flash_charge_duration, 1);

	int32_t max_tone_map_curve_points = 128;
	staticInfo.update(ANDROID_TONEMAP_MAX_CURVE_POINTS,
			  &max_tone_map_curve_points, 1);

	int32_t maxFaces = 1;
	staticInfo.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
			  &maxFaces, 1);

	/* uint8_t timestampSource =
	 * ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN; */
	uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
	staticInfo.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
			  &timestampSource, 1);

	int32_t histogram_size = 64;
	staticInfo.update(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
			  &histogram_size, 1);

	int32_t max_histogram_count = 1000;
	staticInfo.update(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
			  &max_histogram_count, 1);

	int32_t sharpness_map_size[2] = {64, 64};
	staticInfo.update(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE,
			  sharpness_map_size, 2);

	int32_t max_sharpness_map_value = 1000;
	staticInfo.update(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
			  &max_sharpness_map_value, 1);

	int32_t scaler_formats[] = {
		HAL_PIXEL_FORMAT_YV12,
		HAL_PIXEL_FORMAT_YCbCr_420_888,
		HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
		HAL_PIXEL_FORMAT_BLOB};
		//HAL_PIXEL_FORMAT_YCrCb_420_SP};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_FORMATS,
			  scaler_formats, sizeof(scaler_formats)/sizeof(int32_t));

	staticInfo.update(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
			  pixel_array_size, 2);

	staticInfo.update(ANDROID_SCALER_AVAILABLE_RAW_SIZES,
			  pixel_array_size, 2);

	int32_t available_fps_ranges[2] = {15, 30};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
			  available_fps_ranges, 2);

	camera_metadata_rational exposureCompensationStep = {1, 1};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
			  &exposureCompensationStep, 1);

	uint8_t availableVstabModes[1] = {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
			  availableVstabModes, 1);

	/*HAL 1 and HAL 3 common*/
	float maxZoom = 1;
	staticInfo.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
			  &maxZoom, 1);

	uint8_t croppingType = ANDROID_SCALER_CROPPING_TYPE_FREEFORM;
	staticInfo.update(ANDROID_SCALER_CROPPING_TYPE, &croppingType, 1);

	int32_t max3aRegions[3] = {/*AE*/0,/*AWB*/0,/*AF*/0};
	staticInfo.update(ANDROID_CONTROL_MAX_REGIONS,
			  max3aRegions, 3);

	uint8_t availableFaceDetectModes[] = {
		ANDROID_STATISTICS_FACE_DETECT_MODE_OFF/*,*/
		/*ANDROID_STATISTICS_FACE_DETECT_MODE_FULL*/};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
			  availableFaceDetectModes,
			  sizeof(availableFaceDetectModes));

	int32_t exposureCompensationRange[] = {-3, 3};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
			  exposureCompensationRange,
			  sizeof(exposureCompensationRange)/sizeof(int32_t));

	uint8_t lensFacing = (facingBack) ?
		ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
	staticInfo.update(ANDROID_LENS_FACING, &lensFacing, 1);

	int32_t available_thumbnail_sizes[] = {
		160, 120,
		160, 160,
		160, 90,
		144, 96,
		0, 0,
	};
	staticInfo.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
			  available_thumbnail_sizes,
			  sizeof(available_thumbnail_sizes)/sizeof(int32_t));

	static const int32_t jpegMaxSize = 10 * 1024 * 1024; /* 10M */
	staticInfo.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

	int32_t available_stream_configs[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
			  available_stream_configs,
			  sizeof(available_stream_configs)/sizeof(int32_t));

	int64_t available_stall_durations[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0]
		};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
			  available_stall_durations,
			  sizeof(available_stall_durations)/sizeof(int64_t));

	int32_t available_depth_stream_config[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT
		};
	staticInfo.update(ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS,
			  available_depth_stream_config,
			  sizeof(available_depth_stream_config)/sizeof(int32_t));

	/* android.scaler.availableMinFrameDurations */
	int64_t available_frame_min_durations[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0]
		};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
			  available_frame_min_durations,
			  sizeof(available_frame_min_durations)/sizeof(int64_t));

	/* android.scaler.depthAvailableDepthMinFrameDurations */
	int64_t available_depth_frame_min_durations[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0]
		};
	staticInfo.update(ANDROID_DEPTH_AVAILABLE_DEPTH_MIN_FRAME_DURATIONS,
			  available_depth_frame_min_durations,
			  sizeof(available_depth_frame_min_durations)/sizeof(int64_t));

	int64_t available_depth_stall_durations[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		f_duration[0]
		};
	staticInfo.update(ANDROID_DEPTH_AVAILABLE_DEPTH_STALL_DURATIONS,
			  available_depth_stall_durations,
			  sizeof(available_depth_stall_durations)/sizeof(int64_t));

	int32_t available_high_speed_video_config[] = {};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
			  available_high_speed_video_config, 0);

	/* format of the map is : input format, num_output_formats,
	 * outputFormat1,..,outputFormatN */
	int32_t io_format_map[] = {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
		4,
		scaler_formats[0], scaler_formats[1], scaler_formats[2],
		scaler_formats[3]};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP,
			  io_format_map, sizeof(io_format_map)/sizeof(int32_t));

	static const uint8_t hotpixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
	staticInfo.update(ANDROID_HOT_PIXEL_MODE, &hotpixelMode, 1);

	static const uint8_t hotPixelMapMode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
	staticInfo.update(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hotPixelMapMode, 1);
	uint8_t avail_effects[] = {
		ANDROID_CONTROL_EFFECT_MODE_MONO,
		ANDROID_CONTROL_EFFECT_MODE_NEGATIVE,
		ANDROID_CONTROL_EFFECT_MODE_SEPIA,
		ANDROID_CONTROL_EFFECT_MODE_AQUA
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_EFFECTS,
			  avail_effects,sizeof(avail_effects));
	uint8_t avail_scene_modes[] = {
		ANDROID_CONTROL_SCENE_MODE_PORTRAIT,
		ANDROID_CONTROL_SCENE_MODE_LANDSCAPE,
		ANDROID_CONTROL_SCENE_MODE_SPORTS
	};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
			  avail_scene_modes,
			  sizeof(avail_scene_modes));
	uint8_t scene_mode_overrides[] = {
		// ANDROID_CONTROL_SCENE_MODE_PORTRAIT
		ANDROID_CONTROL_AE_MODE_ON,
		ANDROID_CONTROL_AWB_MODE_AUTO,
		ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		// ANDROID_CONTROL_SCENE_MODE_LANDSCAPE
		ANDROID_CONTROL_AE_MODE_ON,
		ANDROID_CONTROL_AWB_MODE_AUTO,
		ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
		// ANDROID_CONTROL_SCENE_MODE_SPORTS
		ANDROID_CONTROL_AE_MODE_ON,
		ANDROID_CONTROL_AWB_MODE_AUTO,
		ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE
	};
	staticInfo.update(ANDROID_CONTROL_SCENE_MODE_OVERRIDES,
			  scene_mode_overrides,
			  sizeof(scene_mode_overrides));

	uint8_t available_control_modes[] = {
		ANDROID_CONTROL_MODE_OFF,
		ANDROID_CONTROL_MODE_AUTO,
		ANDROID_CONTROL_MODE_USE_SCENE_MODE};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_MODES,
			  available_control_modes, 3);

	static const uint8_t avail_antibanding_modes =
		ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
			  &avail_antibanding_modes, 1);

	static const uint8_t avail_abberation_modes =
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
	staticInfo.update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
			  &avail_abberation_modes, 1);

	static const uint8_t avail_af_modes = ANDROID_CONTROL_AF_MODE_OFF;
	staticInfo.update(ANDROID_CONTROL_AF_AVAILABLE_MODES,
			  &avail_af_modes, 1);

	static const uint8_t avail_awb_modes = ANDROID_CONTROL_AWB_MODE_OFF;
	staticInfo.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
			  &avail_awb_modes, 1);

	uint8_t available_flash_levels = 10;
	staticInfo.update(ANDROID_FLASH_FIRING_POWER,
			  &available_flash_levels, 1);

	uint8_t flashAvailable = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
	staticInfo.update(ANDROID_FLASH_INFO_AVAILABLE, &flashAvailable, 1);

	static const uint8_t avail_ae_modes = ANDROID_CONTROL_AE_MODE_OFF;
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_MODES,
			  &avail_ae_modes, 1);

	int32_t sensitivity_range[2] = {100, 1600};
	staticInfo.update(ANDROID_SENSOR_INFO_SENSITIVITY_RANGE,
			  sensitivity_range, 2);

	int32_t sensor_orientation = 0;
	staticInfo.update(ANDROID_SENSOR_ORIENTATION,
			  &sensor_orientation, 1);

	int32_t max_output_streams[3] = {
		1 /*MAX_STALLING_STREAMS*/,
		3 /*MAX_PROCESSED_STREAMS*/,
		1 /*MAX_RAW_STREAMS*/};
	staticInfo.update(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
			  max_output_streams, 3);

	uint8_t avail_leds = 0;
	staticInfo.update(ANDROID_LED_AVAILABLE_LEDS, &avail_leds, 0);
	static const uint8_t focus_dist_calibrated =
		ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED;
	staticInfo.update(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
			  &focus_dist_calibrated, 1);

	int32_t avail_testpattern_modes[2] = {
		ANDROID_SENSOR_TEST_PATTERN_MODE_OFF,
		ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR};
	staticInfo.update(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
			  avail_testpattern_modes, 2);

	uint8_t max_pipeline_depth = 1;
	staticInfo.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
			  &max_pipeline_depth,
			  1);
	/* don't set this meta info if we don't support partial result */
#if 0
	int32_t partial_result_count = 0; /*PARTIAL_RESULT_COUNT;*/
	staticInfo.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
			  &partial_result_count, 1);
#endif
	int32_t max_stall_duration = 1/*MAX_REPROCESS_STALL*/;
	staticInfo.update(ANDROID_REPROCESS_MAX_CAPTURE_STALL,
			  &max_stall_duration, 1);

	uint8_t available_capabilities[8];
	uint8_t available_capabilities_count = 0;
	available_capabilities[available_capabilities_count++] =
	ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
	if (facingBack) {
		available_capabilities[available_capabilities_count++] =
			ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW;
	}
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
			  available_capabilities,
			  available_capabilities_count);

	int32_t max_input_streams = 0;
	staticInfo.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
			  &max_input_streams, 1);

	static const int32_t max_latency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
	staticInfo.update(ANDROID_SYNC_MAX_LATENCY, &max_latency, 1);

	static const uint8_t available_hot_pixel_modes = ANDROID_HOT_PIXEL_MODE_OFF;
	staticInfo.update(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
			  &available_hot_pixel_modes, 1);

	uint8_t available_shading_modes[] = {ANDROID_SHADING_MODE_OFF/*,*/
		                         /*ANDROID_SHADING_MODE_HIGH_QUALITY*/};
	staticInfo.update(ANDROID_SHADING_AVAILABLE_MODES,
			  available_shading_modes,
			  sizeof(available_shading_modes));

	uint8_t available_lens_shading_map_modes[] = {
		ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF
			/*,ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON*/};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
			  available_lens_shading_map_modes,
			  sizeof(available_lens_shading_map_modes));

	static const uint8_t available_edge_modes = ANDROID_EDGE_MODE_OFF;
	staticInfo.update(ANDROID_EDGE_AVAILABLE_EDGE_MODES,
			  &available_edge_modes, 1);

	static const uint8_t available_noise_red_modes =
		ANDROID_NOISE_REDUCTION_MODE_OFF;
	staticInfo.update(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
			  &available_noise_red_modes, 1);

	uint8_t available_tonemap_modes[] = {
		ANDROID_TONEMAP_MODE_CONTRAST_CURVE
	};
	staticInfo.update(ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
			  available_tonemap_modes,
			  sizeof(available_tonemap_modes));

	uint8_t available_hot_pixel_map_modes[] = {
		ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF
	};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
			  available_hot_pixel_map_modes,
			  sizeof(available_tonemap_modes));

	int32_t request_keys_basic[] = {ANDROID_COLOR_CORRECTION_MODE,
	ANDROID_COLOR_CORRECTION_TRANSFORM, ANDROID_COLOR_CORRECTION_GAINS,
	ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
	ANDROID_CONTROL_AE_ANTIBANDING_MODE, ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
	ANDROID_CONTROL_AE_LOCK, ANDROID_CONTROL_AE_MODE,
	ANDROID_CONTROL_AE_REGIONS, ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
	ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, ANDROID_CONTROL_AF_MODE,
	ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AWB_LOCK,
	ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_CAPTURE_INTENT,
	ANDROID_CONTROL_EFFECT_MODE, ANDROID_CONTROL_MODE,
	ANDROID_CONTROL_SCENE_MODE, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
	ANDROID_DEMOSAIC_MODE, ANDROID_EDGE_MODE, ANDROID_EDGE_STRENGTH,
	ANDROID_FLASH_FIRING_POWER, ANDROID_FLASH_FIRING_TIME, ANDROID_FLASH_MODE,
	ANDROID_JPEG_GPS_COORDINATES,
	ANDROID_JPEG_GPS_PROCESSING_METHOD, ANDROID_JPEG_GPS_TIMESTAMP,
	ANDROID_JPEG_ORIENTATION, ANDROID_JPEG_QUALITY, ANDROID_JPEG_THUMBNAIL_QUALITY,
	ANDROID_JPEG_THUMBNAIL_SIZE, ANDROID_LENS_APERTURE,
	ANDROID_LENS_FILTER_DENSITY,
	ANDROID_LENS_FOCAL_LENGTH, ANDROID_LENS_FOCUS_DISTANCE,
	ANDROID_LENS_OPTICAL_STABILIZATION_MODE, ANDROID_NOISE_REDUCTION_MODE,
	ANDROID_NOISE_REDUCTION_STRENGTH, ANDROID_REQUEST_ID, ANDROID_REQUEST_TYPE,
	ANDROID_SCALER_CROP_REGION, ANDROID_SENSOR_EXPOSURE_TIME,
	ANDROID_SENSOR_FRAME_DURATION, ANDROID_HOT_PIXEL_MODE,
	ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
	ANDROID_SENSOR_SENSITIVITY, ANDROID_SHADING_MODE,
	ANDROID_SHADING_STRENGTH, ANDROID_STATISTICS_FACE_DETECT_MODE,
	ANDROID_STATISTICS_HISTOGRAM_MODE, ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
	ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, ANDROID_TONEMAP_CURVE_BLUE,
	ANDROID_TONEMAP_CURVE_GREEN, ANDROID_TONEMAP_CURVE_RED, ANDROID_TONEMAP_MODE,
	ANDROID_BLACK_LEVEL_LOCK};

	size_t request_keys_cnt =
	    sizeof(request_keys_basic)/sizeof(request_keys_basic[0]);
	//NOTE: Please increase available_request_keys array size before
	//adding any new entries.
	int32_t available_request_keys[request_keys_cnt+1];
	memcpy(available_request_keys, request_keys_basic, sizeof(request_keys_basic));
	//NOTE: Please increase available_request_keys array size before
	//adding any new entries.
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
			  available_request_keys, request_keys_cnt);

	int32_t result_keys_basic[] = {ANDROID_COLOR_CORRECTION_TRANSFORM,
	ANDROID_COLOR_CORRECTION_GAINS, ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_REGIONS,
	ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AF_MODE,
	ANDROID_CONTROL_AF_STATE, ANDROID_CONTROL_AWB_MODE,
	ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_MODE, ANDROID_EDGE_MODE,
	ANDROID_FLASH_FIRING_POWER, ANDROID_FLASH_FIRING_TIME, ANDROID_FLASH_MODE,
	ANDROID_FLASH_STATE, ANDROID_JPEG_GPS_COORDINATES, ANDROID_JPEG_GPS_PROCESSING_METHOD,
	ANDROID_JPEG_GPS_TIMESTAMP, ANDROID_JPEG_ORIENTATION, ANDROID_JPEG_QUALITY,
	ANDROID_JPEG_THUMBNAIL_QUALITY, ANDROID_JPEG_THUMBNAIL_SIZE, ANDROID_LENS_APERTURE,
	ANDROID_LENS_FILTER_DENSITY, ANDROID_LENS_FOCAL_LENGTH, ANDROID_LENS_FOCUS_DISTANCE,
	ANDROID_LENS_FOCUS_RANGE, ANDROID_LENS_STATE, ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
	ANDROID_NOISE_REDUCTION_MODE, ANDROID_REQUEST_ID,
	ANDROID_SCALER_CROP_REGION, ANDROID_SHADING_MODE, ANDROID_SENSOR_EXPOSURE_TIME,
	ANDROID_SENSOR_FRAME_DURATION, ANDROID_SENSOR_SENSITIVITY,
	ANDROID_SENSOR_TIMESTAMP, ANDROID_SENSOR_NEUTRAL_COLOR_POINT,
	ANDROID_SENSOR_PROFILE_TONE_CURVE, ANDROID_BLACK_LEVEL_LOCK, ANDROID_TONEMAP_CURVE_BLUE,
	ANDROID_TONEMAP_CURVE_GREEN, ANDROID_TONEMAP_CURVE_RED, ANDROID_TONEMAP_MODE,
	ANDROID_STATISTICS_FACE_DETECT_MODE, ANDROID_STATISTICS_HISTOGRAM_MODE,
	ANDROID_STATISTICS_SHARPNESS_MAP, ANDROID_STATISTICS_SHARPNESS_MAP_MODE,
	ANDROID_STATISTICS_PREDICTED_COLOR_GAINS, ANDROID_STATISTICS_PREDICTED_COLOR_TRANSFORM,
	ANDROID_STATISTICS_SCENE_FLICKER, ANDROID_STATISTICS_FACE_IDS,
	ANDROID_STATISTICS_FACE_LANDMARKS, ANDROID_STATISTICS_FACE_RECTANGLES,
	ANDROID_STATISTICS_FACE_SCORES};
	size_t result_keys_cnt =
	    sizeof(result_keys_basic)/sizeof(result_keys_basic[0]);
	//NOTE: Please increase available_result_keys array size before
	//adding any new entries.

	staticInfo.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
			  result_keys_basic, result_keys_cnt);

	int32_t available_characteristics_keys[] = {ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
	ANDROID_CONTROL_AE_AVAILABLE_MODES, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
	ANDROID_CONTROL_AE_COMPENSATION_RANGE, ANDROID_CONTROL_AE_COMPENSATION_STEP,
	ANDROID_CONTROL_AF_AVAILABLE_MODES, ANDROID_CONTROL_AVAILABLE_EFFECTS,
	ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
	ANDROID_SCALER_CROPPING_TYPE,
	ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
	ANDROID_SYNC_MAX_LATENCY,
	ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
	ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
	ANDROID_CONTROL_AWB_AVAILABLE_MODES, ANDROID_CONTROL_MAX_REGIONS,
	ANDROID_CONTROL_SCENE_MODE_OVERRIDES,ANDROID_FLASH_INFO_AVAILABLE,
	ANDROID_FLASH_INFO_CHARGE_DURATION, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
	ANDROID_JPEG_MAX_SIZE, ANDROID_LENS_INFO_AVAILABLE_APERTURES,
	ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
	ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
	ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
	ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
	ANDROID_LENS_INFO_SHADING_MAP_SIZE,
	ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
	ANDROID_LENS_FACING,
	ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
	ANDROID_REQUEST_PIPELINE_MAX_DEPTH, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
	ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
	ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
	/*ANDROID_REQUEST_PARTIAL_RESULT_COUNT,*/
	ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
	ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP,
	ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
	ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
	ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
	ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS,
	ANDROID_DEPTH_AVAILABLE_DEPTH_MIN_FRAME_DURATIONS,
	ANDROID_DEPTH_AVAILABLE_DEPTH_STALL_DURATIONS,
	ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
	ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
	ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
	ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
	ANDROID_SENSOR_INFO_PHYSICAL_SIZE, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
	ANDROID_SENSOR_INFO_WHITE_LEVEL, ANDROID_SENSOR_BASE_GAIN_FACTOR,
	ANDROID_SENSOR_BLACK_LEVEL_PATTERN, ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY,
	ANDROID_SENSOR_ORIENTATION, ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
	ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
	ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
	ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
	ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
	ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE, ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,
	ANDROID_EDGE_AVAILABLE_EDGE_MODES,
	ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
	ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES,
	ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
	ANDROID_TONEMAP_MAX_CURVE_POINTS,
	ANDROID_CONTROL_AVAILABLE_MODES,
	ANDROID_CONTROL_AE_LOCK_AVAILABLE,
	ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
	ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
	ANDROID_SHADING_AVAILABLE_MODES,
	ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL };
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
		      available_characteristics_keys,
		      sizeof(available_characteristics_keys)/sizeof(int32_t));
	meta = staticInfo.release();
	return meta;
}

int Camera3HWInterface::initialize(const struct camera3_device *device,
				   const camera3_callback_ops_t *callback_ops)
{
	Camera3HWInterface *hw = getPriv(device);

	ALOGD("[%s]\n", __FUNCTION__);

	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized\n",
		      __FUNCTION__);
	}

	hw->mCallbacks = callback_ops;
	return 0;
}

int Camera3HWInterface::configureStreams(const struct camera3_device *device,
					 camera3_stream_configuration_t *stream_list)
{
	Camera3HWInterface *hw = getPriv(device);

	if (!hw) {
		ALOGE("[%s] Camera3HW Interface is not initialized\n",
		      __FUNCTION__);
	}

	if ((stream_list == NULL) || (stream_list->streams == NULL)) {
		ALOGE("[%s] stream configurationg is NULL\n", __FUNCTION__);
	}

	ALOGD("[%s] num_streams:%d \n", __FUNCTION__, stream_list->num_streams);
	for (size_t i = 0; i < stream_list->num_streams; i++) {
		camera3_stream_t *new_stream = stream_list->streams[i];
		if ((new_stream->stream_type == CAMERA3_STREAM_OUTPUT) ||
		    (new_stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL)) {
			new_stream->max_buffers = MAX_BUFFER_COUNT;
			if (new_stream->stream_type == CAMERA3_STREAM_OUTPUT)
				new_stream->usage = GRALLOC_USAGE_HW_CAMERA_READ
					| GRALLOC_USAGE_HW_CAMERA_WRITE;
			else if (new_stream->stream_type ==
				 CAMERA3_STREAM_OUTPUT) {
				if (new_stream->usage &
				    GRALLOC_USAGE_HW_VIDEO_ENCODER)
					new_stream->usage =
						(GRALLOC_USAGE_SW_READ_RARELY |
						GRALLOC_USAGE_SW_WRITE_RARELY |
						GRALLOC_USAGE_HW_CAMERA_WRITE);
				else
					new_stream->usage =
						GRALLOC_USAGE_HW_CAMERA_WRITE;
			}

		}
		ALOGD("[%s] stream type = %d, max_buffer = %d \n",
		      __FUNCTION__, new_stream->stream_type,
		      new_stream->max_buffers);
	}

	return 0;
}

const camera_metadata_t* Camera3HWInterface::constructDefaultRequestSettings(const struct
									     camera3_device
									     *device,
									     int type)
{
	CameraMetadata metaData;
	camera_metadata_t *metaInfo;

	ALOGD("[%s] type = %d\n", __FUNCTION__, type);

	static const uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
	metaData.update(ANDROID_REQUEST_TYPE, &requestType, 1);

	int32_t defaultRequestID = 0;
	metaData.update(ANDROID_REQUEST_ID, &defaultRequestID, 1);

	uint8_t controlIntent = 0;

	switch (type) {
	case CAMERA3_TEMPLATE_PREVIEW:
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
		break;
	default:
		controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
		break;
	}
	metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);

	/*flash */
	static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
	metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);

	metaInfo = metaData.release();
	/*
	 * Todo
	 * check whether there is a need to set static metadata again or not
	 */
	return metaInfo;
}

int Camera3HWInterface::processCaptureRequest(const struct camera3_device *device,
					      camera3_capture_request_t *request)
{
	Camera3HWInterface *hw = getPriv(device);
	int ret;

	ALOGD("[%s] num of output buffers:%d\n", __FUNCTION__,
	      request->num_output_buffers);

	if (hw->mBufControl == NULL) {
		camera3_stream_t *stream = request->output_buffers->stream;
		const camera3_stream_buffer_t *output =
			&request->output_buffers[0];
		private_handle_t *buffer = (private_handle_t*)*output->buffer;

		hw->mBufControl = new Camera3BufferControl(hw->mHandle,
							   hw->mCallbacks);
		ret = hw->mBufControl->initStreaming(buffer);
		if (ret)
			return ret;

	 } else {
		if (!hw->mBufControl->getStreamingMode()) {
			uint32_t index = hw->mBufControl->getQIndex();
			ALOGD("[%s] qbuf count is %d\n", __FUNCTION__, index);
		    if (index >= MIN_BUFFER_COUNT) {
			int ret = hw->mBufControl->registerBuffer(request);
			if (!ret) {
				hw->mBufControl->readyToRun();
				hw->mBufControl->run(String8::format("Camera3BufferControlThread"));
			}
			return 0;
		    }
		}
	 }

	hw->mBufControl->registerBuffer(request);

	return 0;
}

int Camera3HWInterface::flush(const struct camera3_device *device)
{
	Camera3HWInterface *hw  = getPriv(device);

	ALOGD("[%s]\n", __FUNCTION__);

	hw->mBufControl->flush();

	return 0;
}

extern "C" {
camera3_device_ops_t camera3Ops = {
	.initialize			= Camera3HWInterface::initialize,
        .configure_streams		= Camera3HWInterface::configureStreams,
	.register_stream_buffers	= NULL,
	.construct_default_request_settings =
		Camera3HWInterface::constructDefaultRequestSettings,
	.process_capture_request	=
		Camera3HWInterface::processCaptureRequest,
	.get_metadata_vendor_tag_ops	= NULL,
	.dump				= NULL,
	.flush				= Camera3HWInterface::flush,
	.reserved			= {0},
};
} /* extern "C" */

int Camera3HWInterface::cameraDeviceClose(struct hw_device_t* device)
{
	Camera3HWInterface *hw  = getPriv(device);
	camera3_device_t *dev = (camera3_device_t*)device;

	ALOGD("[%s] handle:%d\n", __FUNCTION__, hw->mHandle);

	hw->mBufControl->stopStreaming();

	while(hw->mBufControl->isRunning());

	if (hw->mBufControl != NULL)
		hw->mBufControl = NULL;

	if (hw->mHandle)
		close(hw->mHandle);

	if (dev)
		delete dev;
	if (hw)
		delete hw;

	return 0;
}

Camera3HWInterface::Camera3HWInterface(int cameraId, int fd)
{
	ALOGD("[%s] cameraId:%d, videoHandle:%d\n", __FUNCTION__,
	      cameraId, fd);

	memset(&mCameraDevice, 0x0, sizeof(camera3_device_t));

	mBufControl = NULL;
	mHandle = fd;
	mCameraId = cameraId;
	mCameraDevice.common.tag = HARDWARE_DEVICE_TAG;
	mCameraDevice.common.version = CAMERA_DEVICE_API_VERSION_3_4;
	mCameraDevice.common.close = cameraDeviceClose;
	mCameraDevice.ops = &camera3Ops;
	mCameraDevice.priv =  this;
	ALOGI("tag = %d\n", mCameraDevice.common.tag);
	ALOGI("version = %d\n", mCameraDevice.common.version);
}

Camera3HWInterface::~Camera3HWInterface(void)
{
	ALOGD("[%s] destroyed\n", __FUNCTION__);
}

/*****************************************************************************/
/* Common Camera Hal Interface						     */
/*****************************************************************************/
static int get_number_of_cameras(void)
{
	/*
	 * only count built in camera BACK + FRONT
	 * external camera will be notified
	 * by camera_device_status_change callback
	 */
	int numOfcameras = 1; /* currently just set 1 not 2 */

	ALOGI("[%s] num of cameras:%d", __FUNCTION__, numOfcameras);

	return numOfcameras;
}

static int get_camera_info(int camera_id, struct camera_info *info)
{
	int ret = 0;

	ALOGE("[%s] cameraID:%d", __FUNCTION__, camera_id);

	/* 0 = BACK, 1 = FRONT */
	info->facing = camera_id ? CAMERA_FACING_FRONT :
		CAMERA_FACING_BACK;

	/* The values is not available in the other case */
	if (info->facing != CAMERA_FACING_EXTERNAL)
		info->orientation = 0;

	info->device_version = CAMERA_DEVICE_API_VERSION_3_4;
	info->resource_cost = 100;
	info->conflicting_devices = NULL;
	info->conflicting_devices_length = 0;

	info->static_camera_characteristics =
		Camera3HWInterface::initStaticMetadata(camera_id);
	ALOGI("======camera info =====\n");
	ALOGI("camera facing = %s\n", info->facing ? "Front" : "Back");
	ALOGI("device version = %d\n", info->device_version);
	ALOGI("resource cost = %d\n", info->resource_cost);
	ALOGI("conflicting devices is %s\n", info->conflicting_devices ? "exist"
	      : "not exist");

	return 0;
}

static int set_callbacks(const camera_module_callbacks_t *callbacks)
{
	/*
	 * Need to set callback for torch_mode_status_change()
	 * but currently don't care of things related to flash
	 */
	ALOGD("[%s]\n", __FUNCTION__);

	return 0;
}

static int cameraDeviceOpen(const struct hw_module_t *module,
			    const char *id,
			    struct hw_device_t **device)
{
	int fd = 0, camera_id = 0;

	ALOGD("[%s]\n", __FUNCTION__);

	camera_id = atoi(id);
	if ((camera_id < 0 ) || (camera_id >= get_number_of_cameras()))
		return -EINVAL;

	fd = open("/dev/video6", O_RDWR);
	if (fd < 0) {
		ALOGE("[%s] failed to open clipper_video device:%d\n",
		      __FUNCTION__, fd);
		*device = NULL;
		return -ENODEV;
	}

	Camera3HWInterface *camera3Hal = new Camera3HWInterface(camera_id, fd);
	if (!camera3Hal) {
		ALOGE("[%s] failed to create Camera3HWInterface\n",
		      __FUNCTION__);
		return -ENOMEM;
	}
	*device = &camera3Hal->mCameraDevice.common;

	return 0;
}

static struct hw_module_methods_t camera_module_methods = {
	.open = cameraDeviceOpen,
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
		.reserved = {0},
	},
	.get_number_of_cameras = get_number_of_cameras,
	.get_camera_info = get_camera_info,
	.set_callbacks = set_callbacks,
	.get_vendor_tag_ops = NULL,
	.open_legacy = NULL,
	.set_torch_mode = NULL,
	.init = NULL,
	.reserved = {0}
};
} /* extern C */

}; /* namespace android */
