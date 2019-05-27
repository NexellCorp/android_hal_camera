#ifndef NX_METADATA_H
#define NX_METADATA_H

#include <utils/Timers.h>
#include <hardware/camera3.h>

#include "ExifProcessor.h"

namespace android {

	enum interval_val {
		INTERVAL_MIN = 0,
		INTERVAL_MAX,
	};

	struct crop_info {
		uint32_t left;
		uint32_t top;
		uint32_t width;
		uint32_t height;
	};
	
	camera_metadata_t* translateMetadata
		(uint32_t id,
		const camera_metadata_t *request,
		exif_attribute_t *exif,
		nsecs_t timestamp,
		uint8_t pipeline_depth);

	camera_metadata_t *initStaticMetadata(uint32_t id, uint32_t facing, uint32_t ori, uint32_t fd);

	bool getCropInfo(uint32_t id, struct crop_info *crop);
	void getActiveArraySize(uint32_t id, uint32_t *width, uint32_t *height);
	void getAvaliableResolution(uint32_t id, int *width, int *height);
	bool isSupportedResolution(uint32_t id, uint32_t width, uint32_t height);
	int getJpegResolution(uint32_t size, uint32_t *width, uint32_t *height);
}; /* namespace android */

#endif
