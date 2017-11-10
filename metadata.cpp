namespace android {

#define MAX_SUPPORTED_RESOLUTION 64
#define MAX_SYNC_LATENCY 4

static int32_t checkMinFps(uint32_t count, struct v4l2_frame_interval *f)
{
	uint32_t i;
	uint32_t min = f[0].interval[V4L2_INTERVAL_MIN];

	if (count == 1)
		return min;

	for (i = 1; i < count; i++) {
		if (f[i].interval[V4L2_INTERVAL_MIN] >
		    f[i-1].interval[V4L2_INTERVAL_MIN])
			min = f[i].interval[V4L2_INTERVAL_MIN];
	}
	return min;
}

static int32_t checkMaxFps(uint32_t count, struct v4l2_frame_interval *f)
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

static int32_t checkMaxJpegSize(uint32_t count, struct v4l2_frame_interval *f)
{
	uint32_t i;
	uint32_t max = (f[0].width * f[0].height)*3;

	if (count == 1)
		return max;

	for (i = 1; i < count; i++) {
		if ((f[i].width * f[i].height) > (f[i-1].width * f[i-1].height))
			max = (f[i].width * f[i].height)*3;
	}
	return max;
}

static uint32_t getFrameInfo(int fd, struct v4l2_frame_interval *frames)
{
	int r = 0, ret = 0;

	for (int j = 0; j < MAX_SUPPORTED_RESOLUTION; j++) {
		if (ret)
			break;
		frames[r].index = j;
		ret = v4l2_get_framesize(fd, &frames[r]);
		if (!ret) {
			ALOGD("[%d] width:%d, height:%d",
			      r, frames[r].width, frames[r].height);
			if ((frames[r].width % 32) == 0) {
				for (int i = 0; i <= V4L2_INTERVAL_MAX; i++) {
					ret = v4l2_get_frameinterval(fd,
								     &frames[r],
								     i);
					if (ret) {
						ALOGE("Failed to get interval for width:%d, height:%d",
						      frames[r].width, frames[r].height);
						return r;
					}
					ALOGD("width:%d, height:%d, %s interval:%d",
					      frames[r].width, frames[r].height,
					      (i) ? "max":"min", frames[r].interval[i]);
				}
				r++;
			}
		}
	}
	return r;
}

const camera_metadata_t *initStaticMetadata(uint32_t camera_id, uint32_t fd)
{
	CameraMetadata staticInfo;
	const camera_metadata_t *meta;

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
	int32_t sensor_orientation = 0;
	staticInfo.update(ANDROID_SENSOR_ORIENTATION,
			  &sensor_orientation, 1);

	uint8_t lensFacing = (!camera_id) ?
		ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
	staticInfo.update(ANDROID_LENS_FACING, &lensFacing, 1);
	float focal_lengths = 3.43f;
	staticInfo.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
			  &focal_lengths, 1);

	/* Zoom */
	float maxZoom = 1;
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

	struct v4l2_frame_interval frames[MAX_SUPPORTED_RESOLUTION];
	int r = getFrameInfo(fd, frames);
	ALOGD("[%s] supported resolutions count:%d", __func__, r);
	if (!r) {
		ALOGE("sensor resolution value is invalid");
		return NULL;
	}
	/* pixel size for preview and still capture */
	int32_t pixel_array_size[2] = {frames[0].width, frames[0].height};
	staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
			  pixel_array_size, 2);
	int32_t active_array_size[] = {
		0/*left*/, 0/*top*/,
		pixel_array_size[0]/*width*/,
		pixel_array_size[1]/*height*/
	};
	staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
			  active_array_size, 4);

	int32_t max_fps = checkMaxFps(r, frames);
	int32_t min_fps = checkMinFps(r, frames);
	int32_t available_fps_ranges[] = {
		min_fps, max_fps, max_fps, max_fps
	};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
			  available_fps_ranges,
			  sizeof(available_fps_ranges)/sizeof(int32_t));

	/* For CTS: android.hardware.camera2.cts.RecordingTest#testBasicRecording */
	int64_t max = max_fps * 1e9;
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
	staticInfo.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
			  exposureCompensationRange,
			  sizeof(exposureCompensationRange)/sizeof(int32_t));
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

	int32_t max_latency = (!camera_id) ?
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

	/* TODO: handle variation of sensor */
	/* check whether same format has serveral resolutions */
	int array_size = fmt_count * 4 * r;
	int32_t available_stream_configs[array_size];
	int64_t available_frame_min_durations[array_size];
	for(int f = 0; f < fmt_count; f++) {
		for (int j = 0; j < r ; j++) {
			int offset = f*4*r + j*4;
			ALOGD("[%s] f:%d, j:%d, r:%d, offset:%d", __func__, f, j, r, offset);
			available_stream_configs[offset] = scaler_formats[f];
			available_stream_configs[1+offset] = frames[j].width;
			available_stream_configs[2+offset] = frames[j].height;
			available_stream_configs[3+offset] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
			available_frame_min_durations[offset] = scaler_formats[f];
			available_frame_min_durations[1+offset] = frames[j].width;
			available_frame_min_durations[2+offset] = frames[j].height;
			available_frame_min_durations[3+offset] = available_fps_ranges[0];
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
	int stall_array_size = fmt_count * 4 * r;
	int64_t available_stall_durations[stall_array_size];
	for (int f = 0; f < fmt_count; f++) {
		for (int j = 0; j < r; j++) {
			int offset = f*4*r + j*4;
			available_stall_durations[f+offset] = stall_formats[f];
			available_stall_durations[f+1+offset] = frames[j].width;
			available_stall_durations[f+2+offset] = frames[j].height;
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
	int32_t jpegMaxSize = checkMaxJpegSize(r, frames);
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
	if (camera_id)
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

	meta = staticInfo.release();

	ALOGD("End initStaticMetadata");
	return meta;
}

}; /*namespace android*/
