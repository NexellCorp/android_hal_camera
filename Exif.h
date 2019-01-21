/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * get from exynos libcamera2 source
 */

#ifndef EXIF_H_
#define EXIF_H_

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <log/log.h>
#include "GlobalDef.h"

#define EXIF_LOG2(x)                    (log((double)(x)) / log(2.0))
#define APEX_FNUM_TO_APERTURE(x)        (((EXIF_LOG2((double)(x)) * 200) + 0.5)/100)
#define APEX_EXPOSURE_TO_SHUTTER(x)     ((x) >= 1 ?                                 \
                                        (int)(-(EXIF_LOG2((double)(x)) + 0.5)) :    \
                                        (int)(-(EXIF_LOG2((double)(x)) - 0.5)))
#define APEX_ISO_TO_FILMSENSITIVITY(x)  ((int)(EXIF_LOG2((x) / 3.125) + 0.5))

#define NUM_SIZE                    2
#define IFD_SIZE                    12
#define OFFSET_SIZE                 4

#define NUM_0TH_IFD_TIFF            10
#define NUM_0TH_IFD_EXIF            25 //22
#define NUM_0TH_IFD_GPS             10
#define NUM_1TH_IFD_TIFF            9

/* Type */
#define EXIF_TYPE_BYTE              1
#define EXIF_TYPE_ASCII             2
#define EXIF_TYPE_SHORT             3
#define EXIF_TYPE_LONG              4
#define EXIF_TYPE_RATIONAL          5
#define EXIF_TYPE_UNDEFINED         7
#define EXIF_TYPE_SLONG             9
#define EXIF_TYPE_SRATIONAL         10

#define EXIF_FILE_SIZE              28800
#define EXIF_LIMIT_SIZE             (64*1024)

/* 0th IFD TIFF Tags */
#define EXIF_TAG_IMAGE_WIDTH                    0x0100
#define EXIF_TAG_IMAGE_HEIGHT                   0x0101
#define EXIF_TAG_MAKE                           0x010f
#define EXIF_TAG_MODEL                          0x0110
#define EXIF_TAG_ORIENTATION                    0x0112
#define EXIF_TAG_SOFTWARE                       0x0131
#define EXIF_TAG_DATE_TIME                      0x0132
#define EXIF_TAG_YCBCR_POSITIONING              0x0213
#define EXIF_TAG_EXIF_IFD_POINTER               0x8769
#define EXIF_TAG_GPS_IFD_POINTER                0x8825

/* 0th IFD Exif Private Tags */
#define EXIF_TAG_EXPOSURE_TIME                  0x829A
#define EXIF_TAG_FNUMBER                        0x829D
#define EXIF_TAG_EXPOSURE_PROGRAM               0x8822
#define EXIF_TAG_ISO_SPEED_RATING               0x8827
#define EXIF_TAG_EXIF_VERSION                   0x9000
#define EXIF_TAG_DATE_TIME_ORG                  0x9003
#define EXIF_TAG_DATE_TIME_DIGITIZE             0x9004
#define EXIF_TAG_SHUTTER_SPEED                  0x9201
#define EXIF_TAG_APERTURE                       0x9202
#define EXIF_TAG_BRIGHTNESS                     0x9203
#define EXIF_TAG_EXPOSURE_BIAS                  0x9204
#define EXIF_TAG_MAX_APERTURE                   0x9205
#define EXIF_TAG_METERING_MODE                  0x9207
#define EXIF_TAG_FLASH                          0x9209
#define EXIF_TAG_FOCAL_LENGTH                   0x920A
#define EXIF_TAG_USER_COMMENT                   0x9286
#define EXIF_TAG_SUBSEC_TIME			0x9290
#define EXIF_TAG_SUBSEC_TIME_ORIGINAL		0x9291
#define EXIF_TAG_SUBSEC_TIME_DIGITIZED		0x9292
#define EXIF_TAG_COLOR_SPACE                    0xA001
#define EXIF_TAG_PIXEL_X_DIMENSION              0xA002
#define EXIF_TAG_PIXEL_Y_DIMENSION              0xA003
#define EXIF_TAG_EXPOSURE_MODE                  0xA402
#define EXIF_TAG_WHITE_BALANCE                  0xA403
#define EXIF_TAG_SCENE_CAPTURE_TYPE             0xA406

/* 0th IFD GPS Info Tags */
#define EXIF_TAG_GPS_VERSION_ID                 0x0000
#define EXIF_TAG_GPS_LATITUDE_REF               0x0001
#define EXIF_TAG_GPS_LATITUDE                   0x0002
#define EXIF_TAG_GPS_LONGITUDE_REF              0x0003
#define EXIF_TAG_GPS_LONGITUDE                  0x0004
#define EXIF_TAG_GPS_ALTITUDE_REF               0x0005
#define EXIF_TAG_GPS_ALTITUDE                   0x0006
#define EXIF_TAG_GPS_TIMESTAMP                  0x0007
#define EXIF_TAG_GPS_PROCESSING_METHOD          0x001B
#define EXIF_TAG_GPS_DATESTAMP                  0x001D

/* 1th IFD TIFF Tags */
#define EXIF_TAG_COMPRESSION_SCHEME             0x0103
#define EXIF_TAG_X_RESOLUTION                   0x011A
#define EXIF_TAG_Y_RESOLUTION                   0x011B
#define EXIF_TAG_RESOLUTION_UNIT                0x0128
#define EXIF_TAG_JPEG_INTERCHANGE_FORMAT        0x0201
#define EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LEN    0x0202


typedef enum {
    EXIF_ORIENTATION_UP     = 1,
    EXIF_ORIENTATION_90     = 6,
    EXIF_ORIENTATION_180    = 3,
    EXIF_ORIENTATION_270    = 8,
} ExifOrientationType;

typedef enum {
    EXIF_SCENE_STANDARD,
    EXIF_SCENE_LANDSCAPE,
    EXIF_SCENE_PORTRAIT,
    EXIF_SCENE_NIGHT,
} CamExifSceneCaptureType;

typedef enum {
    EXIF_METERING_UNKNOWN,
    EXIF_METERING_AVERAGE,
    EXIF_METERING_CENTER,
    EXIF_METERING_SPOT,
    EXIF_METERING_MULTISPOT,
    EXIF_METERING_PATTERN,
    EXIF_METERING_PARTIAL,
    EXIF_METERING_OTHER     = 255,
} CamExifMeteringModeType;

typedef enum {
    EXIF_EXPOSURE_AUTO,
    EXIF_EXPOSURE_MANUAL,
    EXIF_EXPOSURE_AUTO_BRACKET,
} CamExifExposureModeType;

typedef enum {
    EXIF_WB_AUTO,
    EXIF_WB_MANUAL,
} CamExifWhiteBalanceType;

/* Values */
#define EXIF_DEF_MAKER          "NEXELL"
#define EXIF_DEF_MODEL          "NEXELL"
#define EXIF_DEF_SOFTWARE       "NEXELL"
#define EXIF_DEF_EXIF_VERSION   "0220"
#define EXIF_DEF_USERCOMMENTS   "User comments"

#define EXIF_DEF_YCBCR_POSITIONING  1   /* centered */
#define EXIF_DEF_FNUMBER_NUM        26  /* 2.6 */
#define EXIF_DEF_FNUMBER_DEN        10
#define EXIF_DEF_EXPOSURE_PROGRAM   3   /* aperture priority */
#define EXIF_DEF_FOCAL_LEN_NUM      343 /* 3.43mm */
#define EXIF_DEF_FOCAL_LEN_DEN      100
#define EXIF_DEF_FLASH              0   /* O: off, 1: on*/
#define EXIF_DEF_COLOR_SPACE        1
#define EXIF_DEF_EXPOSURE_MODE      EXIF_EXPOSURE_AUTO
#define EXIF_DEF_APEX_DEN           100

#define EXIF_DEF_COMPRESSION        6
#define EXIF_DEF_RESOLUTION_NUM     72
#define EXIF_DEF_RESOLUTION_DEN     1
#define EXIF_DEF_RESOLUTION_UNIT    2   /* inches */

#ifdef __cplusplus
struct rational_t {
    friend bool operator==(const rational_t &lhs, const rational_t &rhs);
    friend bool operator!=(const rational_t &lhs, const rational_t &rhs);
    rational_t() : num(0), den(0) {
    }
    rational_t(const rational_t& rhs) : num(rhs.num), den(rhs.den) {
    }
    rational_t& operator=(const rational_t &rhs) {
        if (rhs == *this)
            return (*this);
        this->num = rhs.num;
        this->den = rhs.den;
        return (*this);
    }
#else
typedef struct {
#endif
    uint32_t num;
    uint32_t den;
#ifdef __cplusplus
};
bool operator==(const rational_t &lhs, const rational_t &rhs);
bool operator!=(const rational_t &lhs, const rational_t &rhs);
#else
} rational_t;
#endif

#ifdef __cplusplus
struct srational_t {
    friend bool operator==(const srational_t &lhs, const srational_t &rhs);
    friend bool operator!=(const srational_t &lhs, const srational_t &rhs);
    srational_t() : num(0), den(0) {
    }
    srational_t(const srational_t& rhs) : num(rhs.num), den(rhs.den) {
    }
    srational_t& operator=(const srational_t &rhs) {
        if (rhs == *this)
            return (*this);
        this->num = rhs.num;
        this->den = rhs.den;
        return (*this);
    }
#else
typedef struct {
#endif
    int32_t num;
    int32_t den;
#ifdef __cplusplus
};
bool operator==(const srational_t &lhs, const srational_t &rhs);
bool operator!=(const srational_t &lhs, const srational_t &rhs);
#else
} srational_t;
#endif

#ifdef __cplusplus
struct exif_attribute_t {
    exif_attribute_t()
        : enableGps(false),
          enableThumb(false),
          orientation(EXIF_ORIENTATION_UP),
          ycbcr_positioning(EXIF_DEF_YCBCR_POSITIONING),
          exposure_program(EXIF_DEF_EXPOSURE_PROGRAM),
          iso_speed_rating(0),
          metering_mode(EXIF_METERING_CENTER),
          flash(0),
          color_space(EXIF_DEF_COLOR_SPACE),
          exposure_mode(EXIF_DEF_EXPOSURE_MODE),
          white_balance(EXIF_WB_AUTO),
          scene_capture_type(EXIF_SCENE_STANDARD)
    {
        memset(gps_processing_method, 0, sizeof(gps_processing_method));
        setFixedAttribute();
    }
    exif_attribute_t(const exif_attribute_t &rhs)
        : enableGps(rhs.enableGps),
          enableThumb(rhs.enableThumb),
          width(rhs.width),
          height(rhs.height),
          widthThumb(rhs.widthThumb),
          heightThumb(rhs.heightThumb),
          thumbnailQuality(rhs.thumbnailQuality),
          orientation(rhs.orientation),
          ycbcr_positioning(rhs.ycbcr_positioning),
          exposure_program(rhs.exposure_program),
          iso_speed_rating(rhs.iso_speed_rating),
          metering_mode(rhs.metering_mode),
          flash(rhs.flash),
          color_space(rhs.color_space),
          exposure_mode(rhs.exposure_mode),
          white_balance(rhs.white_balance),
          scene_capture_type(rhs.scene_capture_type),
          x_resolution(rhs.x_resolution),
          y_resolution(rhs.y_resolution),
          resolution_unit(rhs.resolution_unit),
          compression_scheme(rhs.compression_scheme),
          exposure_time(rhs.exposure_time),
          fnumber(rhs.fnumber),
          aperture(rhs.aperture),
          max_aperture(rhs.max_aperture),
          focal_length(rhs.focal_length),
          shutter_speed(rhs.shutter_speed),
          brightness(rhs.brightness),
          exposure_bias(rhs.exposure_bias),
          gps_timestamp_i64(rhs.gps_timestamp_i64),
          gps_altitude_ref(rhs.gps_altitude_ref)
    {
        memcpy(maker, rhs.maker, 32);
        memcpy(model, rhs.model, 32);
        memcpy(software, rhs.software, 32);
        memcpy(exif_version, rhs.exif_version, 4);
        memcpy(date_time, rhs.date_time, 20);
        memcpy(user_comment, rhs.user_comment, 150);
        memcpy(gps_version_id, rhs.gps_version_id, 4);

        memcpy(gps_coordinates, rhs.gps_coordinates, sizeof(double) * 3);
        memcpy(gps_processing_method, rhs.gps_processing_method, 100);

        memcpy(gps_latitude_ref, rhs.gps_latitude_ref, 2);
        memcpy(gps_longitude_ref, rhs.gps_longitude_ref, 2);

        for (int i = 0; i < 3; i++) {
            gps_latitude[i] = rhs.gps_latitude[i];
            gps_longitude[i] = rhs.gps_longitude[i];
            gps_timestamp[i] = rhs.gps_timestamp[i];
        }

        memcpy(gps_datestamp, rhs.gps_datestamp, 11);
    }
    void setFixedAttribute() {
        char property[PROPERTY_VALUE_MAX];

	ALOGDV("setFixedAttribute");

        strncpy((char *)maker, EXIF_DEF_MAKER, sizeof(maker) - 1);
        maker[sizeof(maker) - 1] = '\0';
	ALOGDV("maker:%s", maker);

        property_get("ro.product.model", property, EXIF_DEF_MODEL);
        strncpy((char *)model, property, sizeof(model) - 1);
        model[sizeof(model) - 1] = '\0';
	ALOGDV("model:%s", model);

        strncpy((char *)software, EXIF_DEF_SOFTWARE, sizeof(software) - 1);
        software[sizeof(software) - 1] = '\0';

        memcpy(exif_version, EXIF_DEF_EXIF_VERSION, sizeof(exif_version));

        strcpy((char *)user_comment, EXIF_DEF_USERCOMMENTS);

        gps_version_id[0] = 0x02;
        gps_version_id[1] = 0x02;
        gps_version_id[2] = 0x00;
        gps_version_id[3] = 0x00;

        fnumber.num = 2.7f * EXIF_DEF_FNUMBER_DEN;
        fnumber.den = EXIF_DEF_FNUMBER_DEN;
        aperture.num = APEX_FNUM_TO_APERTURE((double)fnumber.num/fnumber.den) * EXIF_DEF_APEX_DEN;
        aperture.den = EXIF_DEF_APEX_DEN;
        max_aperture.num = aperture.num;
        max_aperture.den = aperture.den;
        exposure_bias.num = 0;
        exposure_bias.den = 0;
        x_resolution.num = EXIF_DEF_RESOLUTION_NUM;
        x_resolution.den = EXIF_DEF_RESOLUTION_DEN;
        y_resolution.num = EXIF_DEF_RESOLUTION_NUM;
        y_resolution.den = EXIF_DEF_RESOLUTION_DEN;
        resolution_unit = EXIF_DEF_RESOLUTION_UNIT;
        compression_scheme = EXIF_DEF_COMPRESSION;
	widthThumb = 0;
	heightThumb = 0;
	orientation = 0;
	cropX = 0;
	cropY = 0;
	cropWidth = 0;
	cropHeight = 0;
    }

    bool setThumbResolution(const uint32_t& widthThumb, const uint32_t& heightThumb) {
        if (widthThumb != this->widthThumb || heightThumb != this->heightThumb) {
            this->widthThumb = widthThumb;
            this->heightThumb = heightThumb;
            return true;
        }
        return false;
    }

    bool setThumbnailQuality(const uint32_t& thumbnailQuality) {
        if (thumbnailQuality != this->thumbnailQuality) {
            this->thumbnailQuality = thumbnailQuality;
            return true;
        }
        return false;
    }

    bool setCropResolution(const uint32_t& x, const uint32_t& y,
			   const uint32_t& widthCrop, const uint32_t& heightCrop) {
	    if ((widthCrop != this->cropWidth) || (heightCrop != this->cropHeight) ||
		    (x != cropX || y != cropY)) {
			    this->cropX = x;
			    this->cropY = y;
			    this->cropWidth = widthCrop;
			    this->cropHeight = heightCrop;
			    return true;
		    }
	    return false;
    }

    bool setOrientation(const uint32_t& orientation) {
        if (orientation != this->orientation) {
            this->orientation = orientation;
            return true;
        }
        return false;
    }

    bool setIsoSpeedRating(const uint32_t& iso_speed_rating) {
        if (iso_speed_rating != this->iso_speed_rating) {
            this->iso_speed_rating = iso_speed_rating;
            return true;
        }
        return false;
    }

    bool setWhiteBalance(const uint32_t& white_balance) {
        if (white_balance != this->white_balance) {
            this->white_balance = white_balance;
            return true;
        }
        return false;
    }

    bool setSceneCaptureType(const uint32_t& scene_capture_type) {
        if (scene_capture_type != this->scene_capture_type) {
            this->scene_capture_type = scene_capture_type;
            return true;
        }
        return false;
    }

    bool setExposureTime(const int64_t &exposure_time) {
        if (exposure_time != this->exposure_time) {
            this->exposure_time = exposure_time;
            return true;
        }
        return false;
    }

    bool setFocalLength(const rational_t &focal_length) {
        if (focal_length != this->focal_length) {
            this->focal_length = focal_length;
            return true;
        }
        return false;
    }

    bool setFlashMode(const uint32_t &flashMode) {
        if (flashMode != this->flash) {
            this->flash = flashMode;
            return true;
        }
        return false;
    }

    bool setGpsCoordinates(const double *gps_coordinates) {
        if (this->gps_coordinates[0] != gps_coordinates[0] ||
            this->gps_coordinates[1] != gps_coordinates[1] ||
            this->gps_coordinates[2] != gps_coordinates[2]) {
            this->gps_coordinates[0] = gps_coordinates[0];
            this->gps_coordinates[1] = gps_coordinates[1];
            this->gps_coordinates[2] = gps_coordinates[2];
            return true;
        }
        return false;
    }

    bool setGpsProcessingMethod(const unsigned char *p, int count) {
        int numCopy = count > 100 ? 100 : count;
        if (gps_processing_method[0] == 0 ||
            strncmp(reinterpret_cast<char const *>(gps_processing_method), reinterpret_cast<char const *>(p), numCopy)) {
            memset(gps_processing_method, 0, sizeof(gps_processing_method));
            for (int i = 0; i < count; i++)
                gps_processing_method[i] = p[i];
            return true;
        }
        return false;
    }

    bool setGpsTimestamp(const int64_t& gps_timestamp_i64) {
        if (gps_timestamp_i64 != this->gps_timestamp_i64) {
            this->gps_timestamp_i64 = gps_timestamp_i64;
            return true;
        }
        return false;
    }
#else
typedef struct {
#endif
    bool enableGps;
    bool enableThumb;

    unsigned char maker[32];
    unsigned char model[32];
    unsigned char software[32];
    unsigned char exif_version[4];
    unsigned char date_time[20];
    unsigned char user_comment[150];
    unsigned char sec_time[7];

    uint32_t width;
    uint32_t height;
    uint32_t widthThumb;
    uint32_t heightThumb;
    uint32_t thumbnailQuality;
    uint32_t cropX;
    uint32_t cropY;
    uint32_t cropWidth;
    uint32_t cropHeight;

    uint16_t orientation;
    uint16_t ycbcr_positioning;
    uint16_t exposure_program;
    uint16_t iso_speed_rating;
    uint16_t metering_mode;
    uint16_t flash;
    uint16_t color_space;
    uint16_t exposure_mode;
    uint16_t white_balance;
    uint16_t scene_capture_type;

    rational_t x_resolution;
    rational_t y_resolution;
    uint16_t resolution_unit;
    uint16_t compression_scheme;

    int64_t exposure_time;
    rational_t fnumber;
    rational_t aperture;
    rational_t max_aperture;
    rational_t focal_length;

    srational_t shutter_speed;
    srational_t brightness; // not handled by command thread
    srational_t exposure_bias;

    uint8_t gps_version_id[4];
    /**
     * gps data handled by command thread
     */
    double gps_coordinates[3];
    unsigned char gps_processing_method[100];
    int64_t   gps_timestamp_i64;

    /**
     * gps data is not handled by command thread
     */
    unsigned char gps_latitude_ref[2];
    unsigned char gps_longitude_ref[2];

    uint8_t gps_altitude_ref;

    rational_t gps_latitude[3];
    rational_t gps_longitude[3];
    rational_t gps_altitude;

    rational_t gps_timestamp[3];
    unsigned char gps_datestamp[11];

    // static constants
    static const unsigned char kSOI[2];
    static const unsigned char kExifHeader[6];
    static const unsigned char kTiffHeader[8];
    static const unsigned char kCode[8];
    static const unsigned char kExifAsciiPrefix[8];
    static const unsigned char kApp1Marker[2];
#ifdef __cplusplus
};
#else
} exif_attribute_t;
#endif

#endif /* EXYNOS_EXIF_H_ */
