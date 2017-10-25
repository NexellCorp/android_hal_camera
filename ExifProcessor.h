#ifndef _NX_EXIF_PROCESSOR_H
#define _NX_EXIF_PROCESSOR_H

#include <gralloc_priv.h>
#include "Exif.h"

namespace android {

class ExifProcessor
{
public:
    // inner class for result
    class ExifResult {
        public:
            ExifResult(unsigned char *result, uint32_t size)
                : Result(result),
                  Size(size) {
            }
            ExifResult(const ExifResult& rhs)
                : Result(rhs.Result),
                  Size(rhs.Size) {
            }
            const unsigned char *getResult() const {
                return Result;
            }
            uint32_t getSize() const {
                return Size;
            }
        private:
            const unsigned char *Result;
            uint32_t Size;
    };

    ExifProcessor()
        : Exif(NULL),
          Width(0),
          Height(0),
          SrcHandle(NULL),
          DstHandle(NULL),
          ScaleHandle(NULL),
          ThumbnailHandle(NULL),
          ThumbnailBuffer(NULL),
          ThumbnailJpegSize(0),
          OutSize(0),
          Allocator(NULL)
    {
    }
    virtual ~ExifProcessor() {
    }

    virtual ExifResult makeExif(
	alloc_device_t *allocator,
	uint32_t width,
	uint32_t height,
	private_handle_t const *srcHandle,
	exif_attribute_t *exif,
	private_handle_t const *dstHandle = NULL);

    virtual void clear() {
	Width = Height = 0;
	SrcHandle = NULL;
	DstHandle = NULL;
	ScaleHandle = NULL;
	ThumbnailHandle = NULL;
	OutBuffer = NULL;
	ThumbnailBuffer = NULL;
	Allocator = NULL;
	OutSize = 0;
	ThumbnailJpegSize = 0;
    }

private:
	exif_attribute_t *Exif;
	uint32_t Width;
	uint32_t Height;
	private_handle_t const *SrcHandle;
	private_handle_t const *DstHandle;
	buffer_handle_t ScaleHandle;
	buffer_handle_t ThumbnailHandle;

	unsigned char *OutBuffer;
	unsigned char *ThumbnailBuffer;

	uint32_t ThumbnailJpegSize;
	uint32_t OutSize;

	alloc_device_t *Allocator;

private:
	virtual ExifResult makeExif();
	virtual bool preprocessExif();
	virtual bool allocOutBuffer();
	virtual bool allocScaleBuffer();
	virtual bool freeScaleBuffer();
	virtual bool allocThumbnailBuffer();
	virtual bool freeThumbnailBuffer();
	virtual bool scaleDown();
	virtual bool encodeThumb();
	virtual bool processExif();
	virtual bool postprocessExif();

	// common inline functions
	ExifResult errorOut() {
	return ExifResult(NULL, 0);
	}

	void writeExifIfd(unsigned char *&pCur, unsigned short tag, unsigned short type, unsigned int count, unsigned int value) {
		memcpy(pCur, &tag, 2);
		pCur += 2;
		memcpy(pCur, &type, 2);
		pCur += 2;
		memcpy(pCur, &count, 4);
		pCur += 4;
		memcpy(pCur, &value, 4);
		pCur += 4;
	}

	void writeExifIfd(unsigned char *&pCur, unsigned short tag, unsigned short type, unsigned int count, unsigned char *pValue) {
		char buf[4] = {0, };

		if (count > 4)
		    return;

		memcpy(buf, pValue, count);
		memcpy(pCur, &tag, 2);
		pCur += 2;
		memcpy(pCur, &type, 2);
		pCur += 2;
		memcpy(pCur, &count, 4);
		pCur += 4;
		memcpy(pCur, buf, 4);
		pCur += 4;
	}

	void writeExifIfd(unsigned char *&pCur, unsigned short tag, unsigned short type, unsigned int count, unsigned char *pValue, unsigned int &offset, unsigned char *start) {
		memcpy(pCur, &tag, 2);
		pCur += 2;
		memcpy(pCur, &type, 2);
		pCur += 2;
		memcpy(pCur, &count, 4);
		pCur += 4;
		memcpy(pCur, &offset, 4);
		pCur += 4;
		memcpy(start + offset, pValue, count);
		offset += count;
	}

	void writeExifIfd(unsigned char *&pCur, unsigned short tag, unsigned short type, unsigned int count, rational_t *pValue, unsigned int &offset, unsigned char *start) {
		memcpy(pCur, &tag, 2);
		pCur += 2;
		memcpy(pCur, &type, 2);
		pCur += 2;
		memcpy(pCur, &count, 4);
		pCur += 4;
		memcpy(pCur, &offset, 4);
		pCur += 4;
		memcpy(start + offset, pValue, 8 * count);
		offset += 8 * count;
	}
};

}; // namespace

#endif
