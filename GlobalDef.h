#ifndef GLOBAL_DEF_H
#define GLOBAL_DEF_H

/* MAX_BUFFER_COUNT musb be bigger than 1 and smaller than 4 for deinterlacing */
#define MAX_BUFFER_COUNT		3
#define NX_MAX_STREAM			4
#define MAX_VIDEO_HANDLES		2

#ifdef BOARD_CAMERA_NUM
#define NUM_OF_CAMERAS			BOARD_CAMERA_NUM
#else
#define NUM_OF_CAMERAS			1
#endif

#ifdef BOARD_CAMERA_BACK_DEVICE
#define BACK_CAMERA_DEVICE		BOARD_CAMERA_BACK_DEVICE
#else
#define BACK_CAMERA_DEVICE		""
#endif

#ifdef BOARD_CAMERA_BACK_ORIENTATION
#define BACK_CAMERA_ORIENTATION		BOARD_CAMERA_BACK_ORIENTATION
#else
#define BACK_CAMERA_ORIENTATION		0
#endif

#ifdef BOARD_CAMERA_BACK_INTERLACED
#define BACK_CAMERA_INTERLACED		BOARD_CAMERA_BACK_INTERLACED
#else
#define BACK_CAMERA_INTERLACED		""
#endif

#ifdef BOARD_CAMERA_BACK_COPY_MODE
#define BACK_CAMERA_COPY_MODE		BOARD_CAMERA_BACK_COPY_MODE
#else
#define BACK_CAMERA_COPY_MODE		""
#endif

#ifdef BOARD_CAMERA_FRONT_DEVICE
#define FRONT_CAMERA_DEVICE		BOARD_CAMERA_FRONT_DEVICE
#else
#define FRONT_CAMERA_DEVICE		""
#endif

#ifdef BOARD_CAMERA_FRONT_ORIENTATION
#define FRONT_CAMERA_ORIENTATION	BOARD_CAMERA_FRONT_ORIENTATION
#else
#define FRONT_CAMERA_ORIENTATION	0
#endif

#ifdef BOARD_CAMERA_FRONT_INTERLACED
#define FRONT_CAMERA_INTERLACED		BOARD_CAMERA_FRONT_INTERLACED
#else
#define FRONT_CAMERA_INTERLACED		""
#endif

#ifdef BOARD_CAMERA_FRONT_COPY_MODE
#define FRONT_CAMERA_COPY_MODE		BOARD_CAMERA_FRONT_COPY_MODE
#else
#define FRONT_CAMERA_COPY_MODE		""
#endif

#ifdef BOARD_NUM_OF_SKIP_FRAMES
#define NUM_OF_SKIP_FRAMES		BOARD_NUM_OF_SKIP_FRAMES
#else
#define NUM_OF_SKIP_FRAMES		0
#endif

/*#define TRACE_STREAM*/
#ifdef TRACE_STREAM
#define ALOGDD(a...)			ALOGD(a)
#define ALOGDI(a...)			ALOGD(a)
#define ALOGDV(a...)			/* ALOGD(a) */
#else
#define ALOGDD(a...)
#define ALOGDI(a...)
#define ALOGDV(a...)
#endif

#endif
