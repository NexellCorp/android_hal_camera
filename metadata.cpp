const camera_metadata_t *initStaticMetadata(int32_t camera_id)
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

	uint8_t lensFacing = (!camera_id) ? ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
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
		ANDROID_STATISTICS_FACE_DETECT_MODE_OFF};
	staticInfo.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
			  availableFaceDetectModes,
			  sizeof(availableFaceDetectModes));

	int32_t maxFaces = 1;
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

	int32_t pixel_array_size[2] = {704, 480};
	staticInfo.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
			  pixel_array_size, 2);
	int32_t active_array_size[] = {0, 0, pixel_array_size[0],
				      pixel_array_size[1]};
	staticInfo.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
			  active_array_size, 4);
	/* AE Mode */
	/* minimum fps duration is not checked in a camera framework */
	int32_t available_fps_ranges[2] = {15, 30};
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
			  available_fps_ranges, 2);

	camera_metadata_rational exposureCompensationStep = {1, 1};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
			  &exposureCompensationStep, 1);

	int32_t exposureCompensationRange[] = {-3, 3};
	staticInfo.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
			  exposureCompensationRange,
			  sizeof(exposureCompensationRange)/sizeof(int32_t));
	/* Format */
	int32_t scaler_formats[] = {
		HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, /* 0x22: same to above */
		HAL_PIXEL_FORMAT_YV12, /* Y/Cr/Cb 420 format */
		HAL_PIXEL_FORMAT_YCbCr_420_888, /* 0x23: Y/Cb/Cr 420 format */
		HAL_PIXEL_FORMAT_BLOB /* 0x21: for jpeg */
	};
	int fmt_count = sizeof(scaler_formats)/sizeof(int32_t);
	staticInfo.update(ANDROID_SCALER_AVAILABLE_FORMATS,
			  scaler_formats, fmt_count);
	/* TODO: handle variation of sensor */
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
		available_fps_ranges[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0]
	};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
			  available_stall_durations,
			  sizeof(available_stall_durations)/sizeof(int64_t));

	/* android.scaler.availableMinFrameDurations */
	int64_t available_frame_min_durations[] = {
		scaler_formats[0],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0],
		scaler_formats[1],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0],
		scaler_formats[2],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0],
		scaler_formats[3],
		pixel_array_size[0], pixel_array_size[1],
		available_fps_ranges[0]
		};
	staticInfo.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
			  available_frame_min_durations,
			  sizeof(available_frame_min_durations)/sizeof(int64_t));

	int32_t available_thumbnail_sizes[] = {
		160, 120, /* width, height */
		160, 160,
		160, 90,
		144, 96,
		0, 0,
	};
	staticInfo.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
			  available_thumbnail_sizes,
			  sizeof(available_thumbnail_sizes)/sizeof(int32_t));

	/* TODO: 10M is too big... */
	/*
	 * size = width * height for BLOB format * scaling factor
	 */
	int32_t jpegMaxSize = 704*480*3;
	staticInfo.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

	uint8_t timestampSource = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
	staticInfo.update(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
			  &timestampSource, 1);
	int32_t available_high_speed_video_config[] = {};
	staticInfo.update(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
			  available_high_speed_video_config, 0);

	/* Effect Mode */
	uint8_t avail_effects[] = {
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
	/* End Scene Mode */

	/* TODO: handle antibanding */
	/* Antibanding Mode */
	uint8_t avail_antibanding_modes =
		ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;
	staticInfo.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
			  &avail_antibanding_modes, 1);

	uint8_t avail_abberation_modes =
		ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
	staticInfo.update(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
			  &avail_abberation_modes, 1);
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

	uint8_t max_pipeline_depth = 8; /* MAX_BUFFER_COUNT; */
	staticInfo.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
					  &max_pipeline_depth, 1);

	/* don't set this meta info if we don't support partial result */
	#if 0
	int32_t partial_result_count = PARTIAL_RESULT_COUNT;
	staticInfo.update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
			  &partial_result_count,
			  1);
	#endif

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

	/* Supported request meta data key arrays */
	int32_t request_keys_basic[] = {
        	ANDROID_CONTROL_AF_TRIGGER,
        	ANDROID_CONTROL_CAPTURE_INTENT,
        	ANDROID_REQUEST_ID,
		ANDROID_REQUEST_TYPE,
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
		ANDROID_CONTROL_AE_STATE,
        	ANDROID_CONTROL_AF_STATE,
        	ANDROID_SENSOR_TIMESTAMP,
	};
	size_t result_keys_cnt =
	    sizeof(result_keys_basic)/sizeof(result_keys_basic[0]);
	//NOTE: Please increase available_result_keys array size before
	//adding any new entries.
	staticInfo.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
					  result_keys_basic, result_keys_cnt);
	meta = staticInfo.release();
	ALOGD("End initStaticMetadata");
	return meta;
}
