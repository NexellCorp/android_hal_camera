#define LOG_TAG "NXStreamManager"
#include <cutils/properties.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <hardware/camera.h>
#include <camera/CameraMetadata.h>

#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>

#include "GlobalDef.h"
#include "StreamManager.h"

#define PREVIEW_USAGE	GRALLOC_USAGE_HW_TEXTURE
#define RECORD_USAGE	GRALLOC_USAGE_HW_VIDEO_ENCODER

namespace android {

camera_metadata_t*
StreamManager::translateMetadata(const camera_metadata_t *request,
		exif_attribute_t *exif,
		nsecs_t timestamp,
		uint8_t pipeline_depth)
{
	CameraMetadata meta;
	camera_metadata_t *result;
	CameraMetadata metaData;

	meta = request;

	dbg_stream("[%s] Exif:%s, timestamp:%ld, pipeline:%d", __func__,
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
		dbg_stream("capture_intent:%d", capture_intent);
		metaData.update(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);
	}

	if (meta.exists(ANDROID_CONTROL_AE_TARGET_FPS_RANGE)) {
		int32_t fps_range[2];
		fps_range[0] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[0];
		fps_range[1] = meta.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE).data.i32[1];
		dbg_stream("ANDROID_CONTROL_AE_TARGET_FPS_RANGE-min:%d,max:%d",
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
			dbg_stream("AF_TRIGGER_START");
			afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
		} else if (trigger == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
			dbg_stream("AF_TRIGGER_CANCELL");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		} else {
			dbg_stream("AF_TRIGGER_IDLE");
			afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
		}
		dbg_stream("ANDROID_CONTROL_AF_STATE:%d", afState);
		metaData.update(ANDROID_CONTROL_AF_STATE, &afState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AF_MODE)) {
		uint8_t afMode = meta.find(ANDROID_CONTROL_AF_MODE).data.u8[0];
		uint8_t afState;
		dbg_stream("ANDROID_CONTROL_AF_MODE:%d", afMode);
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
		dbg_stream("ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION");
		int32_t expCompensation =
			meta.find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
		metaData.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &expCompensation, 1);
	}
	if (meta.exists(ANDROID_CONTROL_MODE)) {
		uint8_t metaMode = meta.find(ANDROID_CONTROL_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_MODE:%d", metaMode);
		metaData.update(ANDROID_CONTROL_MODE, &metaMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_LOCK)) {
		uint8_t aeLock = meta.find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AE_LOCK:%d", aeLock);
		metaData.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_MODE)) {
		uint8_t aeMode = meta.find(ANDROID_CONTROL_AE_MODE).data.u8[0];
		uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
		//uint8_t aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
		dbg_stream("ANDROID_CONTROL_AE_MODE:%d", aeMode);
		if (aeMode != ANDROID_CONTROL_AE_MODE_OFF)
			aeMode = ANDROID_CONTROL_AE_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);
		metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
		uint8_t aeAntiBandingMode = meta.find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AE_ANTIBANDING_MODE:%d", aeAntiBandingMode);
		metaData.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntiBandingMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER) &&
		meta.exists(ANDROID_CONTROL_AE_PRECAPTURE_ID)) {
		uint8_t trigger = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER).data.u8[0];
		uint8_t trigger_id = meta.find(ANDROID_CONTROL_AE_PRECAPTURE_ID).data.u8[0];
		uint8_t aeState;

		dbg_stream("ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER:%d, ID:%d", trigger, trigger_id);
		metaData.update(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &trigger, 1);
		if (trigger == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START) {
			aeState = ANDROID_CONTROL_AE_STATE_LOCKED;
			//uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
			//metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		} else {
			aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
			metaData.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);
		}
		dbg_stream("ANDROID_CONTROL_AE_STATE:%d", aeState);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_LOCK)) {
		uint8_t awbLock =
			meta.find(ANDROID_CONTROL_AWB_LOCK).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AWB_LOCK:%d", awbLock);
		metaData.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
	}
	if (meta.exists(ANDROID_CONTROL_AWB_MODE)) {
		uint8_t awbMode =
			meta.find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_AWB_MODE:%d", awbMode);
		if (awbMode != ANDROID_CONTROL_AWB_MODE_OFF)
			awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
		metaData.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
		uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
		//uint8_t awbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
		dbg_stream("ANDROID_CONTROL_AWB_STATE:%d", awbState);
		metaData.update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);
		if (exif)
			exif->setWhiteBalance(awbMode);
	}
	if (meta.exists(ANDROID_CONTROL_SCENE_MODE)) {
		uint8_t sceneMode = meta.find(ANDROID_CONTROL_SCENE_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_SCENE_MODE:%d", sceneMode);
		metaData.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);
		if (exif)
			exif->setSceneCaptureType(sceneMode);
	}
	/*
	if (meta.exists(ANDROID_COLOR_CORRECTION_MODE)) {
	uint8_t colorCorrectMode =
	meta.find(ANDROID_COLOR_CORRECTION_MODE).data.u8[0];
	dbg_stream("ANDROID_COLOR_CORRECTION_MODE:%d", colorCorrectMode);
	metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectMode, 1);
	}
	*/
	if (meta.exists(ANDROID_COLOR_CORRECTION_ABERRATION_MODE)) {
		uint8_t colorCorrectAbeMode =
			meta.find(ANDROID_COLOR_CORRECTION_ABERRATION_MODE).data.u8[0];
		dbg_stream("ANDROID_COLOR_CORRECTION_ABERRATION_MODE:%d", colorCorrectAbeMode);
		metaData.update(ANDROID_COLOR_CORRECTION_MODE, &colorCorrectAbeMode, 1);
	}
	if (meta.exists(ANDROID_FLASH_MODE)) {
		uint8_t flashMode = meta.find(ANDROID_FLASH_MODE).data.u8[0];
		uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
		dbg_stream("ANDROID_FLASH_MODE:%d", flashMode);
		if (flashMode != ANDROID_FLASH_MODE_OFF)
			flashState = ANDROID_FLASH_STATE_READY;
		dbg_stream("ANDROID_FLASH_STATE:%d", flashState);
		metaData.update(ANDROID_FLASH_STATE, &flashState, 1);
		metaData.update(ANDROID_FLASH_MODE, &flashMode, 1);
		if (exif)
			exif->setFlashMode(flashMode);
	}
	/*
	if (meta.exists(ANDROID_EDGE_MODE)) {
		uint8_t edgeMode = meta.find(ANDROID_EDGE_MODE).data.u8[0];
		dbg_stream("ANDROID_EDGE_MODE:%d", edgeMode);
		metaData.update(ANDROID_EDGE_MODE, &edgeMode, 1);
	}
	if (meta.exists(ANDROID_HOT_PIXEL_MODE)) {
		uint8_t hotPixelMode =
		meta.find(ANDROID_HOT_PIXEL_MODE).data.u8[0];
		dbg_stream("ANDROID_HOT_PIXEL_MODE:%d", hotPixelMode);
		metaData.update(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
	}
	*/
	if (meta.exists(ANDROID_LENS_FOCAL_LENGTH)) {
		float focalLength =
			meta.find(ANDROID_LENS_FOCAL_LENGTH).data.f[0];
		dbg_stream("ANDROID_LENS_FOCAL_LENGTH:%f", focalLength);
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
		dbg_stream("ANDROID_LENS_OPTICAL_STABILIZATION_MODE:%d", optStabMode);
		metaData.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optStabMode, 1);
	}
	if (meta.exists(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE)) {
		uint8_t vsMode = meta.find(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE).data.u8[0];
		dbg_stream("ANDROID_CONTROL_VIDEO_STABILIZATION_MODE:%d", vsMode);
		metaData.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vsMode, 1);
	}
	if (meta.exists(ANDROID_SCALER_CROP_REGION)) {
		int32_t scalerCropRegion[4];
		scalerCropRegion[0] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[0];
		scalerCropRegion[1] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[1];
		scalerCropRegion[2] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[2];
		scalerCropRegion[3] = meta.find(ANDROID_SCALER_CROP_REGION).data.i32[3];
		dbg_stream("ANDROID_SCALER_CROP_REGION:left-%d,top-%d,width-%d,height-%d",
				scalerCropRegion[0], scalerCropRegion[1], scalerCropRegion[2],
				scalerCropRegion[3]);
		metaData.update(ANDROID_SCALER_CROP_REGION, scalerCropRegion, 4);
		if (exif)
			exif->setCropResolution(scalerCropRegion[0], scalerCropRegion[1],
					scalerCropRegion[2], scalerCropRegion[3]);
	} else {
		if (exif)
			exif->setCropResolution(0, 0, 0, 0);
	}

	if (meta.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
		int64_t sensorExpTime =
			meta.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
		dbg_stream("ANDROID_SENSOR_EXPOSURE_TIME:%ld", sensorExpTime);
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
			dbg_stream("minFrame:%ld", minFrameDuration);
			minFrameDuration = (long) 1e9/ minFrameDuration;
	}
	dbg_stream("ANDROID_SENSOR_FRAME_DURATION:%ld, Min:%ld",
			sensorFrameDuration, minFrameDuration);
	if (sensorFrameDuration < minFrameDuration)
		sensorFrameDuration = minFrameDuration;
		metaData.update(ANDROID_SENSOR_FRAME_DURATION, &sensorFrameDuration, 1);
	}
	/*
	if (meta.exists(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW)) {
		int64_t sensorRollingShutterSkew =
		meta.find(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW).data.i64[0];
		dbg_stream("ANDROID_SENSOR_ROLLING_SHUTTER_SKEW:%ld", sensorRollingShutterSkew);
		metaData.update(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &sensorRollingShutterSkew, 1);
	}
	*/
	if (meta.exists(ANDROID_SHADING_MODE)) {
		uint8_t  shadingMode =
			meta.find(ANDROID_SHADING_MODE).data.u8[0];
		dbg_stream("ANDROID_SHADING_MODE:%d", shadingMode);
		metaData.update(ANDROID_SHADING_MODE, &shadingMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_FACE_DETECT_MODE)) {
		uint8_t fwk_facedetectMode =
			meta.find(ANDROID_STATISTICS_FACE_DETECT_MODE).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_FACE_DETECT_MODE:%d", fwk_facedetectMode);
		metaData.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &fwk_facedetectMode, 1);
	}
	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP)) {
		uint8_t sharpnessMapMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_LENS_SHADING_MAP:%d", sharpnessMapMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP, &sharpnessMapMode, 1);
	}
#if 0
	if (meta.exists(ANDROID_COLOR_CORRECTION_GAINS)) {
		float colorCorrectGains[4];
		colorCorrectGains[0] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[0];
		colorCorrectGains[1] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[1];
		colorCorrectGains[2] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[2];
		colorCorrectGains[3] = meta.find(ANDROID_COLOR_CORRECTION_GAINS).data.f[3];
		dbg_stream("ANDROID_COLOR_CORRECTION_GAINS-ColorGain:%f,%f,%f,%f",
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
		dbg_stream("ANDROID_COLOR_CORRECTION_TRANSFORM:%d/%d, %d/%d, %d/%d",
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
		dbg_stream("ANDROID_CONTROL_EFFECT_MODE:%d", fwk_effectMode);
		metaData.update(ANDROID_CONTROL_EFFECT_MODE, &fwk_effectMode, 1);
	}
	if (meta.exists(ANDROID_SENSOR_TEST_PATTERN_MODE)) {
		int32_t fwk_testPatternMode =
			meta.find(ANDROID_SENSOR_TEST_PATTERN_MODE).data.i32[0];
		dbg_stream("ANDROID_SENSOR_TEST_PATTERN_MODE:%d", fwk_testPatternMode);
		metaData.update(ANDROID_SENSOR_TEST_PATTERN_MODE, &fwk_testPatternMode, 1);
	}
	if (meta.exists(ANDROID_JPEG_ORIENTATION)) {
		int32_t orientation =
			meta.find(ANDROID_JPEG_ORIENTATION).data.i32[0];
		dbg_stream("ANDROID_JPEG_ORIENTATION:%d", orientation);
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
		dbg_stream("ANDROID_JPEG_QUALITY:%d", quality);
		metaData.update(ANDROID_JPEG_QUALITY, &quality, 1);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
		uint8_t thumb_quality =
			meta.find(ANDROID_JPEG_THUMBNAIL_QUALITY).data.u8[0];
		dbg_stream("ANDROID_JPEG_THUMBNAIL_QUALITY:%d", thumb_quality);
		metaData.update(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumb_quality, 1);
		if (exif)
			exif->setThumbnailQuality(thumb_quality);
	}
	if (meta.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
		int32_t size[2];
		size[0] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[0];
		size[1] = meta.find(ANDROID_JPEG_THUMBNAIL_SIZE).data.i32[1];
		dbg_stream("ANDROID_JPEG_THUMBNAIL_SIZE- width:%d, height:%d",
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
		dbg_stream("ANDROID_JPEG_GPS_COORDINATES-%f:%f:%f", gps[0], gps[1], gps[2]);
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
		dbg_stream("ANDROID_JPEG_GPS_PROCESSING_METHOD count:%d",
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
		dbg_stream("ANDROID_JPEG_GPS_TIMESTAMP:%lld", timestamp);
		if (exif)
			exif->setGpsTimestamp(timestamp);
		metaData.update(ANDROID_JPEG_GPS_TIMESTAMP, &timestamp, 1);
	}

	if (meta.exists(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE)) {
		uint8_t shadingMode =
			meta.find(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_LENS_SHADING_MAP_MODE:%d", shadingMode);
		metaData.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &shadingMode, 1);
	}
	/*
	if (meta.exists(ANDROID_STATISTICS_SCENE_FLICKER)) {
		uint8_t sceneFlicker =
		meta.find(ANDROID_STATISTICS_SCENE_FLICKER).data.u8[0];
		dbg_stream("ANDROID_STATISTICS_SCENE_FLICKER:%d", sceneFlicker);
		metaData.update(ANDROID_STATISTICS_SCENE_FLICKER, &sceneFlicker, 1);
	}
	*/
	result = metaData.release();
	return result;
}

int StreamManager::jpegEncoding(private_handle_t *dst, private_handle_t *src, exif_attribute_t *exif)
{
	android_ycbcr srcY;
	void *dstV;
	int ret;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	if (exif == NULL) {
		ALOGE("[%s] Exif is NULL", __func__);
		return -EINVAL;
	}
	dbg_stream("start jpegEncoding");

	ret = module->lock(module, dst, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			dst->width, dst->height, &dstV);
	if (ret) {
		ALOGE("failed to lock for dst");
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			src->width, src->height, &srcY);
	if (ret) {
		ALOGE("Failed to lock for src");
		module->unlock(module, dst);
		return ret;
	}
	dbg_stream("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
			dstV, dst->size);

	/* make exif */
	ExifProcessor::ExifResult result =
		mExifProcessor.makeExif(mAllocator, src->width, src->height, src, exif, dst);
	mExifProcessor.clear();
	int exifSize = result.getSize();
	if (!exifSize) {
		ALOGE("Failed to make Exif");
		ret = -EINVAL;
		goto unlock;
	}
	dbg_stream("Exif size:%d", exifSize);

	int skipSOI;
	int jpegSize;
	int jpegBufSize;
	char *jpegBuf;
	camera3_jpeg_blob_t *jpegBlob;
	unsigned char *planar[3];
	planar[0] = (unsigned char*)srcY.y;
	planar[1] = (unsigned char*)srcY.cb;
	planar[2] = (unsigned char*)srcY.cr;
	jpegSize = NX_JpegEncoding((unsigned char *)dstV+exifSize, src->size,
			(unsigned char const *)planar, src->width,
			src->height, srcY.ystride, srcY.cstride, 100,
			NX_PIXFORMAT_YUV420);
	if (jpegSize <= 0) {
		ALOGE("Failed to NX_JpegEncoding!!!");
		ret = -EINVAL;
		goto unlock;
	}
	if (exifSize)
		skipSOI = 2/*SOI*/;
	else
		skipSOI = 0;

	jpegSize = jpegSize - skipSOI;
	memcpy((unsigned char*)dstV+exifSize, (unsigned char*)dstV+exifSize + skipSOI, jpegSize);

	jpegBufSize = dst->size;
	jpegBuf = (char *) dstV;
	jpegBlob = (camera3_jpeg_blob_t *)(&jpegBuf[jpegBufSize -
					sizeof(camera3_jpeg_blob_t)]);
	jpegBlob->jpeg_size = jpegSize + exifSize;
	jpegBlob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
	dbg_stream("capture success: jpegSize(%d), totalSize(%d)",
	jpegSize, jpegBlob->jpeg_size);
	ret = 0;

unlock:
	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("Failed to gralloc unlock for dst:%d\n", ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("Failed to gralloc unlock for src:%d\n", ret);
		return ret;
	}

	dbg_stream("end jpegEncoding");
	return ret;
}

int StreamManager::copyBuffer(private_handle_t *dst, private_handle_t *src)
{
	android_ycbcr dstY, srcY;

	hw_module_t const *pmodule = NULL;
	gralloc_module_t const *module = NULL;
	hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &pmodule);
	module = reinterpret_cast<gralloc_module_t const *>(pmodule);

	int ret = module->lock_ycbcr(module, dst, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			dst->width, dst->height, &dstY);
	if (ret) {
		ALOGE("Failed to lock for dst");
		return ret;
	}

	ret = module->lock_ycbcr(module, src, GRALLOC_USAGE_SW_READ_MASK, 0, 0,
			src->width, src->height, &srcY);
	if (ret) {
		ALOGE("Failed to lock for src");
		return ret;
	}

	dbg_stream("src: %p(%d) --> dst: %p(%d)", srcY.y, src->size,
			dstY.y, dst->size);

	if ((src->width == dst->width) && (src->height == dst->height)) {
		if ((srcY.cstride == dstY.cstride) && (srcY.cstride == dstY.cstride))
			memcpy(dstY.y, srcY.y, src->size);
		else {
			dbg_stream("src and dst buffer has a different alingment");
			for (int i = 0; i < src->height; i++) {
				unsigned long srcOffset = i * srcY.ystride;
				unsigned long srcCbCrOffset = (i/2) * srcY.cstride;
				unsigned long dstOffset = i * dstY.ystride;
				unsigned long dstCbCrOffset = (i/2) * dstY.cstride;
				memcpy((void*)((unsigned long)dstY.y + dstOffset),
						(void*)((unsigned long)srcY.y + srcOffset),
						dstY.ystride);
				if (i%2 == 0) {
					memcpy((void*)((unsigned long)dstY.cb + dstCbCrOffset),
							(void*)((unsigned long)srcY.cb + srcCbCrOffset),
							dstY.cstride);
					memcpy((void*)((unsigned long)dstY.cr + dstCbCrOffset),
							(void*)((unsigned long)srcY.cr + srcCbCrOffset),
							dstY.cstride);
				}
			}
		}
	}

	ret = module->unlock(module, dst);
	if (ret) {
		ALOGE("Failed to gralloc unlock for dst:%d\n", ret);
		return ret;
	}
	ret = module->unlock(module, src);
	if (ret) {
		ALOGE("Failed to gralloc unlock for src:%d\n", ret);
		return ret;
	}

	return 0;
}

void StreamManager::setCaptureResult(uint32_t type, NXCamera3Buffer *buf)
{
	dbg_stream("[%s] get result from %d frame, type:%d", __func__,
			buf->getFrameNumber(), type);

	if (type > NX_SNAPSHOT_STREAM)
		ALOGE("Invalied type");
	else {
		mResultQ[type].queue(buf);
		if (!isRunning()) {
			dbg_stream("START StreamManager Thread");
			run("StreamManagerThread");
		}
	}
}

void StreamManager::getCaptureResult(const struct nx_camera3_callback_ops *ops,
		uint32_t type,
		NXCamera3Buffer *buf)
{
	StreamManager *d = const_cast<StreamManager*>(static_cast<const StreamManager*>(ops->priv));
	d->setCaptureResult(type, buf);
}

int StreamManager::configureStreams(camera3_stream_configuration_t *stream_list)
{
	dbg_stream("[%s]", __func__);

	for (size_t i = 0; i < stream_list->num_streams; i++) {
		camera3_stream_t *stream = stream_list->streams[i];
		dbg_stream("[%zu] format:0x%x, width:%d, height:%d, usage:0x%x",
				i, stream->format, stream->width, stream->height, stream->usage);
		mStream[i] = new Stream(mFd[0], mScaler, mAllocator, &mResultCb, stream, NX_MAX_STREAM);
		if (mStream[i] == NULL) {
			ALOGE("Failed to create stream:%d", i);
			return -EINVAL;
		}

	}

	return NO_ERROR;
}

sp<Stream> StreamManager::getStream(uint32_t type, camera3_stream_t *ph, int usage)
{
	sp<Stream> stream = NULL;
	int j;

	if (ph) {
		for (j = 0; j < NX_MAX_STREAM; j++) {
			if ((mStream[j] != NULL) &&
					(mStream[j]->isThisStream(ph))) {
				stream = mStream[j];
				break;
			}
		}
	} else if(usage) {
		for (j = 0; j < NX_MAX_STREAM; j++)
		{
			if ((mStream[j] != NULL) && (mStream[j]->getUsage() & usage)) {
				stream = mStream[j];
				break;
			}
		}

	} else {
		for (j = 0; j < NX_MAX_STREAM; j++)
		{
			if ((mStream[j] != NULL) && (mStream[j]->getMode() == type)) {
				stream = mStream[j];
				break;
			}
		}
	}

	return stream;
}

int StreamManager::registerRequests(camera3_capture_request_t *r)
{
	CameraMetadata setting, meta;
	sp<Stream> stream;
	const camera3_stream_buffer_t *b;
	int ret = NO_ERROR;

	setting = r->settings;
	meta = r->settings;

	dbg_stream("[%s] frame number:%d, num_output_buffers:%d", __func__,
			r->frame_number, r->num_output_buffers);

	if (setting.exists(ANDROID_REQUEST_ID)) {
		if (mPipeLineDepth == MAX_BUFFER_COUNT)
			mPipeLineDepth = 1;
		else
			mPipeLineDepth++;
	}
	for (uint32_t i = 0; i < r->num_output_buffers; i++) {
		const camera3_stream_buffer_t *b = &r->output_buffers[i];

		if ((b == NULL) || (b->status)) {
			ALOGE("buffer or status is not valid to use:%d", b->status);
			return -EINVAL;
		}

		private_handle_t *ph = (private_handle_t *)*b->buffer;
		if (ph->share_fd < 0) {
			ALOGE("Invalid Buffer --> no fd");
			return -EINVAL;
		}
		dbg_stream("format:0x%x, width:%d, height:%d, size:%d",
				ph->format, ph->width, ph->height, ph->size);
		stream = getStream(NX_MAX_STREAM, b->stream, 0);
		if (stream == NULL) {
			ALOGE("Failed to get stream for this buffer");
			return -EINVAL;
		}
		ret = stream->registerBuffer(r->frame_number, b, meta.release());
		if (ret) {
			ALOGE("Failed to register Buffer for buffer:%d",
					ret);
			return ret;
		}
	}

	if (setting.exists(ANDROID_CONTROL_CAPTURE_INTENT)) {
		uint8_t intent =
		setting.find(ANDROID_CONTROL_CAPTURE_INTENT).data.u8[0];
		dbg_stream("intent = %d, framenumber:%d, num of buffers:%d",
				intent, r->frame_number, r->num_output_buffers);
		if (mMode != intent) {
			if ((intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) &&
					(mMode == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD)) {
				intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
				dbg_stream("[%s] snapshot request is received as still capture",
						__func__);
			}
			dbg_stream("receive new request for %d, previous:%d",
					intent, mMode);
			if ((intent == ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW) ||
					(intent == ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG)) {
				stream = getStream(NX_CAPTURE_STREAM, 0, 0);
				if ((stream != NULL) && (stream->isRunning()))
					stream->stopStreaming();
				stream = getStream(NX_RECORD_STREAM, 0, 0);
				if ((stream != NULL) && (stream->isRunning()))
					stream->stopStreaming();
				stream = getStream(NX_PREVIEW_STREAM, 0, 0);
				if (stream != NULL) {
					if (!stream->isRunning()) {
						if (stream->prepareForRun() == NO_ERROR)
							stream->run("Preview Stream Thread");
						else
							return -EINVAL;
					}
				} else {
					stream = getStream(NX_MAX_STREAM, 0, PREVIEW_USAGE);
					if (stream == NULL) {
						ALOGE("Failed to get stream for this buffer");
						return -EINVAL;
					}
					stream->setMode(NX_PREVIEW_STREAM);
					if (stream->prepareForRun() == NO_ERROR)
						stream->run("Preview Stream Thread");
					else
						return -EINVAL;
				}
			} else if (intent == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE) {
				stream = getStream(NX_PREVIEW_STREAM, 0, 0);
				if ((stream != NULL) && (stream->isRunning()))
					stream->stopStreaming();
				stream = getStream(NX_CAPTURE_STREAM, 0, 0);
				if (stream != NULL) {
					if (!stream->isRunning()) {
						stream->skipFrames();
						if (stream->prepareForRun() == NO_ERROR)
							stream->run("Capture Stream Thread");
						else
							return -EINVAL;
					}
				} else {
					stream = getStream(NX_MAX_STREAM, 0, 0);
					if (stream == NULL) {
						ALOGE("Failed to get stream for this buffer");
						return -EINVAL;
					}
					stream->setMode(NX_CAPTURE_STREAM);
					stream->skipFrames();
					if (stream->prepareForRun() == NO_ERROR)
						stream->run("Capture Stream Thread");
					else
						return -EINVAL;
				}
			} else if (intent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD) {
				stream = getStream(NX_CAPTURE_STREAM, 0, 0);
				if ((stream != NULL) && (stream->isRunning()))
					stream->stopStreaming();
				stream = getStream(NX_SNAPSHOT_STREAM, 0, 0);
				if ((stream != NULL) && (stream->isRunning()))
					stream->stopStreaming();
				if (r->num_output_buffers == 1) {
					stream = getStream(NX_RECORD_STREAM, 0, 0);
					if ((stream != NULL) && (stream->isRunning()))
						stream->stopStreaming();
				}
				stream = getStream(NX_PREVIEW_STREAM, 0, 0);
				if (stream != NULL) {
					if (!stream->isRunning()) {
						stream->setHandle(mFd[0]);
						if (stream->prepareForRun() == NO_ERROR)
							stream->run("Preview Stream Thread");
						else
							return -EINVAL;
					}
				} else {
					stream = getStream(NX_MAX_STREAM, 0, PREVIEW_USAGE);
					if (stream == NULL) {
						ALOGE("Failed to get stream for this buffer");
						return -EINVAL;
					}
					stream->setMode(NX_PREVIEW_STREAM);
					stream->setHandle(mFd[0]);
					if (stream->prepareForRun() == NO_ERROR)
						stream->run("Preview Stream Thread");
				}
				if (r->num_output_buffers > 1) {
					stream = getStream(NX_RECORD_STREAM, 0, 0);
					if (stream != NULL) {
						if (!stream->isRunning()) {
							if (stream->prepareForRun() == NO_ERROR)
								stream->run("Record Stream Thread");
							else
								return -EINVAL;
						}
					} else {
						stream = getStream(NX_MAX_STREAM, 0, RECORD_USAGE);
						if (stream == NULL) {
							ALOGE("Failed to get stream for this buffer");
							return -EINVAL;
						}
						stream->setMode(NX_RECORD_STREAM);
						stream->setHandle(mFd[MAX_VIDEO_HANDLES-1]);
						if (stream->prepareForRun() == NO_ERROR)
							stream->run("Record Stream Thread");
						else
							return -EINVAL;
					}
				}
			}
			else if (intent ==
					ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT) {
				stream = getStream(NX_SNAPSHOT_STREAM, 0, 0);
				if (stream != NULL) {
					if (!stream->isRunning()) {
						if (stream->prepareForRun() == NO_ERROR)
							stream->run("Capture Stream Thread");
						else
							return -EINVAL;
					}
				} else {
					stream = getStream(NX_MAX_STREAM, 0, 0);
					if (stream == NULL) {
						ALOGE("Failed to get stream for this buffer");
						return -EINVAL;
					}
					stream->setMode(NX_SNAPSHOT_STREAM);
					if (stream->prepareForRun() == NO_ERROR)
						stream->run("Snapshot Stream Thread");
					else
						return -EINVAL;
				}
			}
			mMode = intent;
		}
	}
	nx_camera_request_t *request =
	(nx_camera_request_t*)malloc(sizeof(nx_camera_request_t));
	if (!request) {
		ALOGE("Failed to malloc for request");
		ret = -ENOMEM;
	}
	request->frame_number = r->frame_number;
	request->num_output_buffers = r->num_output_buffers;
	request->meta = setting.release();
	mRequestQ.queue(request);
	return ret;
out:
	return -EINVAL;
}

int StreamManager::stopStream()
{
	int ret = NO_ERROR, i;

	for (i = 0; i <NX_MAX_STREAM; i++) {
		if ((mStream[i] != NULL) && (mStream[i]->isRunning()))
			mStream[i]->stopStreaming();
	}

	while (!mRequestQ.isEmpty()) {
		dbg_stream("Wait buffer drained");
		usleep(1000);
	}

	if (isRunning()) {
		dbg_stream("requestExitAndWait Enter", __func__);
		requestExitAndWait();
		dbg_stream("requestExitAndWait Exit", __func__);
	}
	dbg_stream("[%s]", __func__);

	return ret;
}

int StreamManager::sendResult(bool drain)
{
	int ret = NO_ERROR;

	dbg_stream("[%s] Enter", __func__);

	nx_camera_request_t *request = mRequestQ.dequeue();
	if (!request) {
		ALOGE("Failed to get request from Queue");
		return -EINVAL;
	}

	/* notify */
	nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	camera3_notify_msg_t msg;
	memset(&msg, 0x0, sizeof(camera3_notify_msg_t));
	msg.type = CAMERA3_MSG_SHUTTER;
	msg.message.shutter.frame_number = request->frame_number;
	msg.message.shutter.timestamp = timestamp;
	mCb->notify(mCb, &msg);

	/* send result */
	private_handle_t *preview = NULL, *record = NULL,
			 *capture = NULL, *snapshot = NULL;
	camera3_capture_result_t result;
	exif_attribute_t *exif = NULL;
	bzero(&result, sizeof(camera3_capture_result_t));
	result.frame_number = request->frame_number;
	result.num_output_buffers = request->num_output_buffers;

	camera3_stream_buffer_t output_buffers[result.num_output_buffers];
	for (uint32_t i = 0; i < result.num_output_buffers; i++) {
		sp<Stream> stream = NULL;
		NXCamera3Buffer *buf = mRQ.dequeue();
		if (buf) {
			output_buffers[i].stream = buf->getStream();
			output_buffers[i].buffer = buf->getBuffer();
			output_buffers[i].release_fence = -1;
			output_buffers[i].acquire_fence = -1;
			output_buffers[i].status = 0;
		} else {
			ALOGE("Failed to get buffer form RQ");
			break;
		}
		stream = getStream(NX_MAX_STREAM, buf->getStream(), 0);
		if (stream != NULL) {
			if (stream->getMode() == NX_PREVIEW_STREAM) {
				dbg_stream("preview");
				preview =
					(private_handle_t *)buf->getPrivateHandle();
			} else if (stream->getMode() == NX_CAPTURE_STREAM){
				dbg_stream("capture");
				capture =
					(private_handle_t *)buf->getPrivateHandle();
			} else if (stream->getMode() == NX_RECORD_STREAM) {
				dbg_stream("record");
				record =
					(private_handle_t *)buf->getPrivateHandle();
			} else if (stream->getMode() == NX_SNAPSHOT_STREAM) {
				dbg_stream("snapshot");
				snapshot =
					(private_handle_t *)buf->getPrivateHandle();
			}
			if ((stream->getFormat() == HAL_PIXEL_FORMAT_BLOB) &&
					(exif == NULL)) {
					exif = new exif_attribute_t();
					if (!exif)
						ALOGE("[%s] Failed to make exif", __func__);
			}
		} else
			dbg_stream("setream is null\n");
	}

	result.result = translateMetadata(request->meta, exif, timestamp,
			mPipeLineDepth);

	if (preview != NULL) {
		if ((record != NULL) && (MAX_VIDEO_HANDLES == 1)) {
			dbg_stream("copy for record");
			copyBuffer(record, preview);
		}
		if (capture != NULL) {
			dbg_stream("copy for capture");
			if (capture->format == HAL_PIXEL_FORMAT_BLOB)
				jpegEncoding(capture, preview, exif);
			else if (MAX_VIDEO_HANDLES == 1)
				copyBuffer(capture, preview);
		}
		if (snapshot != NULL) {
			dbg_stream("copy for snapshot");
			if (snapshot->format == HAL_PIXEL_FORMAT_BLOB)
				jpegEncoding(snapshot, preview, exif);
			else
				copyBuffer(snapshot, preview);
		}
	}
	result.output_buffers = output_buffers;
	result.partial_result = 1;
	result.input_buffer = NULL;
	dbg_stream("[%s] frame_number:%d, num_output_buffers:%d", __func__,
			result.frame_number, result.num_output_buffers);

	mCb->process_capture_result(mCb, &result);
	if (request)
		free(request);
	dbg_stream("[%s] Exit", __func__);

	return 0;
}

void StreamManager::drainBuffer()
{
	int ret = NO_ERROR;

	dbg_stream("[%s] Enter", __func__);

	while (!mRequestQ.isEmpty())
		sendResult(true);

	dbg_stream("[%s] Exit", __func__);
}

status_t StreamManager::readyToRun()
{
	int ret = NO_ERROR;

	dbg_stream("[%s]", __func__);
	return ret;
}

bool StreamManager::threadLoop()
{
	if (mRequestQ.size() > 0)
	{
		nx_camera_request_t *request = mRequestQ.getHead();
		if (!request) {
			ALOGE("[%s] Failed to get request from Queue",
				__func__);
		return false;
	}
	uint32_t frame_number = request->frame_number;
	uint32_t num_buffers = request->num_output_buffers;
	if (mNumBuffers)
		num_buffers = mNumBuffers;
	for (int i = 0; i < NX_MAX_STREAM; i++) {
		int size = mResultQ[i].size();
		if (size > 0) {
			NXCamera3Buffer *buf =
			mResultQ[i].getHead();
			if ((buf) && (buf->getFrameNumber() == frame_number)) {
				dbg_stream("got a buffer for the frame_buffer:%d from %d",
						frame_number, i);
				buf = mResultQ[i].dequeue();
				mRQ.queue(buf);
				num_buffers--;
				dbg_stream("left buffers:%d", num_buffers);
				if (num_buffers == 0) {
					dbg_stream("got all:%d buffers", request->num_output_buffers);
					sendResult(false);
					mNumBuffers = num_buffers;
					break;
				} else
					mNumBuffers = num_buffers;
				}
			}
		}
	}
	return true;
}

}; /* namespace android */
