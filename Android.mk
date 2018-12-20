LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_CFLAGS :=

ifneq ($(BOARD_CAMERA_NUM),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_NUM=$(BOARD_CAMERA_NUM)
endif

ifneq ($(BOARD_CAMERA_BACK_DEVICE),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_BACK_DEVICE='$(BOARD_CAMERA_BACK_DEVICE)'
endif

ifneq ($(BOARD_CAMERA_BACK_ORIENTATION),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_BACK_ORIENTATION='$(BOARD_CAMERA_BACK_ORIENTATION)'
endif

ifneq ($(BOARD_CAMERA_BACK_INTERLACED),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_BACK_INTERLACED='$(BOARD_CAMERA_BACK_INTERLACED)'
endif

ifneq ($(BOARD_CAMERA_BACK_COPY_MODE),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_BACK_COPY_MODE='$(BOARD_CAMERA_BACK_COPY_MODE)'
endif

ifneq ($(BOARD_CAMERA_FRONT_DEVICE),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_FRONT_DEVICE='$(BOARD_CAMERA_FRONT_DEVICE)'
endif

ifneq ($(BOARD_CAMERA_FRONT_ORIENTATION),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_FRONT_ORIENTATION='$(BOARD_CAMERA_FRONT_ORIENTATION)'
endif

ifneq ($(BOARD_CAMERA_FRONT_INTERLACED),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_FRONT_INTERLACED='$(BOARD_CAMERA_FRONT_INTERLACED)'
endif

ifneq ($(BOARD_CAMERA_FRONT_COPY_MODE),)
	LOCAL_CFLAGS += -DBOARD_CAMERA_FRONT_COPY_MODE='$(BOARD_CAMERA_FRONT_COPY_MODE)'
endif

ifeq ($(BOARD_CAMERA_USE_ZOOM), true)
	LOCAL_CFLAGS += -DCAMERA_USE_ZOOM
else
ifeq ($(BOARD_CAMERA_SUPPORT_SCALING), true)
	LOCAL_CFLAGS += -DCAMERA_SUPPORT_SCALING
endif
endif

ifneq ($(BOARD_CAMERA_SKIP_FRAME),)
	LOCAL_CFLAGS += -DBOARD_NUM_OF_SKIP_FRAMES=$(BOARD_CAMERA_SKIP_FRAME)
endif

LOCAL_SRC_FILES := \
	Camera3HWInterface.cpp \
	StreamManager.cpp \
	Stream.cpp \
	Exif.cpp \
	v4l2.cpp \
	metadata.cpp \
	ExifProcessor.cpp
LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libutils \
	libhardware \
	libcamera_metadata \
	libcamera_client \
	libsync \
	libnxjpeg \
	libnx_scaler \
	libnx_deinterlacer
LOCAL_C_INCLUDES += \
	system/media/camera/include \
	system/media/core/include \
	system/core/libsync/include \
	system/core/include/utils \
	frameworks/native/include \
	frameworks/av/include \
	device/nexell/library/nx-scaler \
	device/nexell/library/nx-deinterlacer \
	$(LOCAL_PATH)/../gralloc \
	$(LOCAL_PATH)/../libnxjpeg \
	external/libjpeg-turbo \
	$(call include-path-for)

include $(BUILD_SHARED_LIBRARY)