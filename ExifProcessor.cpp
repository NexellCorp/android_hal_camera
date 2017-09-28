#define LOG_TAG "ExifProcessor"

#include <sys/types.h>

#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>

#include <utils/Log.h>

#include <libnxjpeg.h>

#include "ExifProcessor.h"

using namespace android;

bool ExifProcessor::preprocessExif()
{
    // enableThumb
    if (Exif->widthThumb > 0 && Exif->heightThumb > 0) {
        Exif->enableThumb = true;
        ALOGD("Thumb Enabled(%dx%d)", Exif->widthThumb, Exif->heightThumb);
    } else {
        Exif->enableThumb = false;
    }
    ALOGD("enableThumb: %d", Exif->enableThumb);

    // width, height
    Exif->width = Width;
    Exif->height = Height;

    // date time
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)Exif->date_time, 20, "%Y:%m:%d %H:%M:%S", timeinfo);
    //strftime((char *)Exif->secTime, 3, "%S",timeinfo);

    uint32_t av, tv, bv, sv, ev;
    av = APEX_FNUM_TO_APERTURE((double)Exif->fnumber.num / Exif->fnumber.den);
    tv = APEX_EXPOSURE_TO_SHUTTER((double)Exif->exposure_time);
    sv = APEX_ISO_TO_FILMSENSITIVITY(Exif->iso_speed_rating);
    bv = av + tv - sv;
    ev = av + tv;

    // shutter speed
    Exif->shutter_speed.num = tv * EXIF_DEF_APEX_DEN;
    Exif->shutter_speed.den = EXIF_DEF_APEX_DEN;

    // brightness
    Exif->brightness.num = bv*EXIF_DEF_APEX_DEN;
    Exif->brightness.den = EXIF_DEF_APEX_DEN;

    // gps
    if (Exif->gps_coordinates[0] != 0 && Exif->gps_coordinates[1] != 0) {
        Exif->enableGps = true;
        ALOGD("GPS Enabled");

        if (Exif->gps_coordinates[0] > 0)
            strcpy((char *)Exif->gps_latitude_ref, "N");
        else
            strcpy((char *)Exif->gps_latitude_ref, "S");

        if (Exif->gps_coordinates[1] > 0)
            strcpy((char *)Exif->gps_longitude_ref, "E");
        else
            strcpy((char *)Exif->gps_longitude_ref, "W");

        if (Exif->gps_coordinates[2] > 0)
            Exif->gps_altitude_ref = 0;
        else
            Exif->gps_altitude_ref = 1;

        double latitude = fabs(Exif->gps_coordinates[0]);
        double longitude = fabs(Exif->gps_coordinates[1]);
        double altitude = fabs(Exif->gps_coordinates[2]);

        Exif->gps_latitude[0].num = (uint32_t)latitude;
        Exif->gps_latitude[0].den = 1;
        Exif->gps_latitude[1].num = (uint32_t)((latitude - Exif->gps_latitude[0].num) * 60);
        Exif->gps_latitude[1].den = 1;
        Exif->gps_latitude[2].num = (uint32_t)round((((latitude - Exif->gps_latitude[0].num) * 60) - Exif->gps_latitude[1].num) * 60);
        Exif->gps_latitude[2].den = 1;

        Exif->gps_longitude[0].num = (uint32_t)longitude;
        Exif->gps_longitude[0].den = 1;
        Exif->gps_longitude[1].num = (uint32_t)((longitude - Exif->gps_longitude[0].num) * 60);
        Exif->gps_longitude[1].den = 1;
        Exif->gps_longitude[2].num = (uint32_t)round((((longitude - Exif->gps_longitude[0].num) * 60) - Exif->gps_longitude[1].num) * 60);
        Exif->gps_longitude[2].den = 1;

        Exif->gps_altitude.num = (uint32_t)round(altitude);
        Exif->gps_altitude.den = 1;

        struct tm tm_data;
        long timestamp = (long)Exif->gps_timestamp_i64;
        gmtime_r(&timestamp, &tm_data);
        Exif->gps_timestamp[0].num = tm_data.tm_hour;
        Exif->gps_timestamp[0].den = 1;
        Exif->gps_timestamp[1].num = tm_data.tm_min;
        Exif->gps_timestamp[1].den = 1;
        Exif->gps_timestamp[2].num = tm_data.tm_sec;
        Exif->gps_timestamp[2].den = 1;
        snprintf((char *)Exif->gps_datestamp, sizeof(Exif->gps_datestamp),
                "%04d:%02d:%02d", tm_data.tm_year + 1900, tm_data.tm_mon + 1, tm_data.tm_mday);
    } else {
        Exif->enableGps = false;
        ALOGD("GPS Disabled");
    }

    return true;
}

bool ExifProcessor::allocOutBuffer()
{
    uint32_t outBufSize = EXIF_FILE_SIZE;
    if (Exif->enableThumb)
        outBufSize += ThumbnailJpegSize;

    if (!DstHandle) {
            ALOGE("failed to allocate Exif Out Buffer(size %u)", outBufSize);
            return false;
    } else {
        OutBuffer = reinterpret_cast<unsigned char *>(DstHandle->base);
    }

    memset(OutBuffer, 0, outBufSize);
    return true;
}

bool ExifProcessor::processExif()
{
    unsigned char *pCur, *pApp1Start, *pIfdStart, *pGpsIfdPtr, *pNextIfdOffset;
    unsigned int tmp = 0, LongerTagOffset = 0, exifSizeExceptThumb;

    pApp1Start = pCur = OutBuffer;

    if (DstHandle) {
        // SOI: 2byte
        memcpy(pCur, exif_attribute_t::kSOI, 2);
        pCur += 2;
        pApp1Start += 2;
    }
    // Skip 4bytes for APP1 Market(2byte), Length(2byte)
    pCur += 4;

    // Exif Header: 6byte
    memcpy(pCur, exif_attribute_t::kExifHeader, 6);
    pCur += 6;

    // TIFF Header: 8byte
    memcpy(pCur, exif_attribute_t::kTiffHeader, 8);
    pIfdStart = pCur;
    pCur += 8;

    // 0th IFD Number of Directory Entry
    if (Exif->enableGps)
        tmp = NUM_0TH_IFD_TIFF;
    else
        tmp = NUM_0TH_IFD_TIFF -1;

    memcpy(pCur, &tmp, NUM_SIZE);
    pCur += NUM_SIZE;

    LongerTagOffset = 8 + NUM_SIZE + (tmp * IFD_SIZE) + OFFSET_SIZE; // Next IFD Offset

    writeExifIfd(pCur, EXIF_TAG_IMAGE_WIDTH, EXIF_TYPE_LONG, 1, Exif->width);
    writeExifIfd(pCur, EXIF_TAG_IMAGE_HEIGHT, EXIF_TYPE_LONG, 1, Exif->height);
    writeExifIfd(pCur, EXIF_TAG_MAKE, EXIF_TYPE_ASCII, strlen((char *)Exif->maker) + 1, Exif->maker, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_MODEL, EXIF_TYPE_ASCII, strlen((char *)Exif->model) + 1, Exif->model, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_ORIENTATION, EXIF_TYPE_SHORT, 1, Exif->orientation);
    writeExifIfd(pCur, EXIF_TAG_SOFTWARE, EXIF_TYPE_ASCII, strlen((char *)Exif->software) + 1, Exif->software, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_DATE_TIME, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_SUBSEC_TIME, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_YCBCR_POSITIONING, EXIF_TYPE_SHORT, 1, Exif->ycbcr_positioning);
    writeExifIfd(pCur, EXIF_TAG_EXIF_IFD_POINTER, EXIF_TYPE_LONG, 1, LongerTagOffset);

    if (Exif->enableGps) {
        pGpsIfdPtr = pCur;
        pCur += IFD_SIZE;
    }

    pNextIfdOffset = pCur;

    // 0th IFD Exif Private Tags
    pCur = pIfdStart + LongerTagOffset;

    tmp = NUM_0TH_IFD_EXIF;
    memcpy(pCur, &tmp, NUM_SIZE);
    pCur += NUM_SIZE;

    LongerTagOffset += NUM_SIZE + (NUM_0TH_IFD_EXIF * IFD_SIZE) + OFFSET_SIZE;

    writeExifIfd(pCur, EXIF_TAG_EXPOSURE_TIME, EXIF_TYPE_LONG, 1, Exif->exposure_time);
    writeExifIfd(pCur, EXIF_TAG_FNUMBER, EXIF_TYPE_RATIONAL, 1, &Exif->fnumber, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_EXPOSURE_PROGRAM, EXIF_TYPE_SHORT, 1, Exif->exposure_program);
    writeExifIfd(pCur, EXIF_TAG_ISO_SPEED_RATING, EXIF_TYPE_SHORT, 1, Exif->iso_speed_rating);
    writeExifIfd(pCur, EXIF_TAG_EXIF_VERSION, EXIF_TYPE_UNDEFINED, 4, Exif->exif_version);
    writeExifIfd(pCur, EXIF_TAG_DATE_TIME_ORG, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_DATE_TIME_DIGITIZE, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_SUBSEC_TIME_ORIGINAL, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_SUBSEC_TIME_DIGITIZED, EXIF_TYPE_ASCII, 20, Exif->date_time, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_SHUTTER_SPEED, EXIF_TYPE_SRATIONAL, 1, (rational_t *)&Exif->shutter_speed, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_APERTURE, EXIF_TYPE_RATIONAL, 1, &Exif->aperture, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_BRIGHTNESS, EXIF_TYPE_SRATIONAL, 1, (rational_t *)&Exif->brightness, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_EXPOSURE_BIAS, EXIF_TYPE_SRATIONAL, 1, (rational_t *)&Exif->exposure_bias, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_MAX_APERTURE, EXIF_TYPE_RATIONAL, 1, &Exif->max_aperture, LongerTagOffset, pIfdStart);
    writeExifIfd(pCur, EXIF_TAG_METERING_MODE, EXIF_TYPE_SHORT, 1, Exif->metering_mode);
    writeExifIfd(pCur, EXIF_TAG_FLASH, EXIF_TYPE_SHORT, 1, Exif->flash);
    writeExifIfd(pCur, EXIF_TAG_FOCAL_LENGTH, EXIF_TYPE_RATIONAL, 1, &Exif->focal_length, LongerTagOffset, pIfdStart);

    memmove(Exif->user_comment + sizeof(exif_attribute_t::kCode), Exif->user_comment, strlen((char *)Exif->user_comment) + 1);
    memcpy(Exif->user_comment, exif_attribute_t::kCode, sizeof(exif_attribute_t::kCode));
    writeExifIfd(pCur, EXIF_TAG_USER_COMMENT, EXIF_TYPE_UNDEFINED,
            strlen((char *)Exif->user_comment) + 1 + sizeof(exif_attribute_t::kCode),
            Exif->user_comment, LongerTagOffset, pIfdStart);

    writeExifIfd(pCur, EXIF_TAG_COLOR_SPACE, EXIF_TYPE_SHORT, 1, Exif->color_space);
    writeExifIfd(pCur, EXIF_TAG_PIXEL_X_DIMENSION, EXIF_TYPE_LONG, 1, Exif->width);
    writeExifIfd(pCur, EXIF_TAG_PIXEL_Y_DIMENSION, EXIF_TYPE_LONG, 1, Exif->height);
    writeExifIfd(pCur, EXIF_TAG_EXPOSURE_MODE, EXIF_TYPE_LONG, 1, Exif->exposure_mode);
    writeExifIfd(pCur, EXIF_TAG_WHITE_BALANCE, EXIF_TYPE_LONG, 1, Exif->white_balance);
    writeExifIfd(pCur, EXIF_TAG_SCENE_CAPTURE_TYPE, EXIF_TYPE_LONG, 1, Exif->scene_capture_type);

    tmp = 0;
    memcpy(pCur, &tmp, OFFSET_SIZE);
    pCur += OFFSET_SIZE;

    // 0th IFD GPS Info
    if (Exif->enableGps) {
        writeExifIfd(pGpsIfdPtr, EXIF_TAG_GPS_IFD_POINTER, EXIF_TYPE_LONG, 1, LongerTagOffset);

        pCur = pIfdStart + LongerTagOffset;

        if (Exif->gps_processing_method[0] == 0) {
            tmp = NUM_0TH_IFD_GPS - 1;
        } else {
            tmp = NUM_0TH_IFD_GPS;
        }
        memcpy(pCur, &tmp, NUM_SIZE);
        pCur += NUM_SIZE;

        LongerTagOffset += NUM_SIZE + (tmp * IFD_SIZE) + OFFSET_SIZE;

        writeExifIfd(pCur, EXIF_TAG_GPS_VERSION_ID, EXIF_TYPE_BYTE, 4, Exif->gps_version_id);
        writeExifIfd(pCur, EXIF_TAG_GPS_LATITUDE_REF, EXIF_TYPE_ASCII, 2, Exif->gps_latitude_ref);
        writeExifIfd(pCur, EXIF_TAG_GPS_LATITUDE, EXIF_TYPE_RATIONAL, 3, Exif->gps_latitude, LongerTagOffset, pIfdStart);
        writeExifIfd(pCur, EXIF_TAG_GPS_LONGITUDE_REF, EXIF_TYPE_ASCII, 2, Exif->gps_longitude_ref);
        writeExifIfd(pCur, EXIF_TAG_GPS_LONGITUDE, EXIF_TYPE_RATIONAL, 3, Exif->gps_longitude, LongerTagOffset, pIfdStart);
        writeExifIfd(pCur, EXIF_TAG_GPS_ALTITUDE_REF, EXIF_TYPE_BYTE, 1, Exif->gps_altitude_ref);
        writeExifIfd(pCur, EXIF_TAG_GPS_ALTITUDE, EXIF_TYPE_RATIONAL, 1, &Exif->gps_altitude, LongerTagOffset, pIfdStart);
        writeExifIfd(pCur, EXIF_TAG_GPS_TIMESTAMP, EXIF_TYPE_RATIONAL, 3, Exif->gps_timestamp, LongerTagOffset, pIfdStart);
        tmp = strlen((char *)Exif->gps_processing_method);
        if (tmp > 0) {
            if (tmp > 100)
                tmp = 100;

            unsigned char tmp_buf[100 + sizeof(exif_attribute_t::kExifAsciiPrefix)];
            memcpy(tmp_buf, exif_attribute_t::kExifAsciiPrefix, sizeof(exif_attribute_t::kExifAsciiPrefix));
            memcpy(&tmp_buf[sizeof(exif_attribute_t::kExifAsciiPrefix)], Exif->gps_processing_method, tmp);
            writeExifIfd(pCur, EXIF_TAG_GPS_PROCESSING_METHOD, EXIF_TYPE_UNDEFINED,
                    tmp + sizeof(exif_attribute_t::kExifAsciiPrefix), tmp_buf, LongerTagOffset, pIfdStart);
        }
        writeExifIfd(pCur, EXIF_TAG_GPS_DATESTAMP, EXIF_TYPE_ASCII, 11, Exif->gps_datestamp, LongerTagOffset, pIfdStart);
        tmp = 0;
        memcpy(pCur, &tmp, OFFSET_SIZE);
        pCur += OFFSET_SIZE;
    }
    // 1th IFD TIFF
    int iThumbFd = 0;
    tmp = 0;
    memcpy(pNextIfdOffset, &tmp, OFFSET_SIZE);

    // APP1 Marker
    memcpy(pApp1Start, exif_attribute_t::kApp1Marker, 2);
    pApp1Start += 2;

    // APP1 Data Size
    tmp = 10 + LongerTagOffset - 2;
    unsigned char app_data_size[2];
    app_data_size[0] = (unsigned char)((tmp >> 8) & 0xFF);
    app_data_size[1] = (unsigned char)(tmp & 0xFF);
    memcpy(pApp1Start, app_data_size, 2);

    // calc OutSize
    OutSize = 2 + 2 + 2 + 6 + LongerTagOffset; // SOI + APP1 Marker + APP1 Size + Exif Header + TIFF
    ALOGD("OutSize: %d, TIFF Size: %d", OutSize, LongerTagOffset);

    ALOGD("[EXIF] %s", OutBuffer);
    return true;
}

bool ExifProcessor::postprocessExif()
{
    return true;
}

ExifProcessor::ExifResult ExifProcessor::makeExif()
{
    bool ret = true;

    ALOGD("makeExif() entered");
    ret = preprocessExif();
    if (!ret) {
        ALOGE("failed to preprocessExif()!!!");
        return errorOut();
    }

    ret = allocOutBuffer();
    if (!ret) {
        ALOGE("failed to allocOutBuffer()!!!");
        return errorOut();
    }

    ret = processExif();
    if (!ret) {
        ALOGE("failed to processExif()!!!");
        return errorOut();
    }

    ret = postprocessExif();
    if (!ret) {
        ALOGE("failed to postprocessExif()!!!");
        return errorOut();
    }

    ALOGD("makeExif() exit");

    return ExifProcessor::ExifResult(OutBuffer, OutSize);
}

ExifProcessor::ExifResult ExifProcessor::makeExif(
        uint32_t width,
        uint32_t height,
        private_handle_t const *srcHandle,
        exif_attribute_t *exif,
        private_handle_t const *dstHandle)
{
    Exif = exif;
    Width = width;
    Height = height;
    SrcHandle = srcHandle;
    DstHandle = dstHandle;
    Exif->widthThumb = 0;
    Exif->heightThumb = 0;
    return makeExif();
}
