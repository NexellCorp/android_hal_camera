#include "Exif.h"

const unsigned char exif_attribute_t::kSOI[2] = {0xFF, 0xD8};
const unsigned char exif_attribute_t::kExifHeader[6] = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};
const unsigned char exif_attribute_t::kTiffHeader[8] = {0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
const unsigned char exif_attribute_t::kCode[8] = {0x00, 0x00, 0x00, 0x49, 0x49, 0x43, 0x53, 0x41};
const unsigned char exif_attribute_t::kExifAsciiPrefix[8] = {0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0};
const unsigned char exif_attribute_t::kApp1Marker[2] = {0xff, 0xe1};

bool operator==(const rational_t &lhs, const rational_t &rhs)
{
    return (lhs.num == rhs.num) && (lhs.den == rhs.den);
}

bool operator!=(const rational_t &lhs, const rational_t &rhs)
{
    return !operator==(lhs, rhs);
}

bool operator==(const srational_t &lhs, const srational_t &rhs)
{
    return (lhs.num == rhs.num) && (lhs.den == rhs.den);
}

bool operator!=(const srational_t &lhs, const srational_t &rhs)
{
    return !operator==(lhs, rhs);
}
