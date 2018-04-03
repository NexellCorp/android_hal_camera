#ifndef NX_METADATA_H
#define NX_METADATA_H

#include <utils/Timers.h>
#include <hardware/camera3.h>

#include "ExifProcessor.h"

namespace android {

camera_metadata_t* translateMetadata
	(const camera_metadata_t *request,
	 exif_attribute_t *exif,
	 nsecs_t timestamp,
	 uint8_t pipeline_depth);

const camera_metadata_t *initStaticMetadata(uint32_t facing, uint32_t ori, uint32_t fd);

void getActiveArraySize(uint32_t *width, uint32_t *height);
}; /* namespace android */

#endif
