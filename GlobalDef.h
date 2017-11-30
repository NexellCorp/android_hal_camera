#ifndef GLOBAL_DEF_H
#define GLOBAL_DEF_H

#define MAX_BUFFER_COUNT		8

#ifdef BOARD_CAMERA_BACK_DEVICE
#define BACK_CAMERA_DEVICE		BOARD_CAMERA_BACK_DEVICE
#else
#define BACK_CAMERA_DEVICE		"/dev/video6"
#endif

#ifdef BOARD_CAMERA_FRONT_DEVICE
#define FRONT_CAMERA_DEVICE		BOARD_CAMERA_FRONT_DEVICE
#else
#define FRONT_CAMERA_DEVICE		"/dev/video7"
#endif

#ifdef BOARD_CAMERA_NUM
#define NUM_OF_CAMERAS			BOARD_CAMERA_NUM
#else
#define NUM_OF_CAMERAS			1
#endif

#ifdef CAMERA_INTERLACED
#define MAX_VIDEO_HANDLES		1
#else
#define MAX_VIDEO_HANDLES		2
#endif

#ifdef BOARD_NUM_OF_SKIP_FRAMES
#define NUM_OF_SKIP_FRAMES		BOARD_NUM_OF_SKIP_FRAMES
#else
#define NUM_OF_SKIP_FRAMES		0
#endif

/*#define TRACE_STREAM*/
#ifdef TRACE_STREAM
#define dbg_stream(a...)		ALOGD(a)
#else
#define dbg_stream(a...)
#endif

#endif
