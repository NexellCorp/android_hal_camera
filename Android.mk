LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_CFLAGS :=
ifneq ($(BOARD_CAMERA_BACK_DEVICE), )
LOCAL_CFLAGS += -DBOARD_CAMERA_BACK_DEVICE='$(BOARD_CAMERA_BACK_DEVICE)'
endif
ifneq ($(BOARD_CAMERA_FRONT_DEVICE), )
LOCAL_CFLAGS += -DBOARD_CAMERA_FRONT_DEVICE='$(BOARD_CAMERA_FRONT_DEVICE)'
endif
ifneq ($(BOARD_CAMERA_NUM), )
LOCAL_CFLAGS += -DBOARD_CAMERA_NUM=$(BOARD_CAMERA_NUM)
endif

LOCAL_SRC_FILES := \
	Camera3HWInterface.cpp \
	StreamManager.cpp \
	v4l2.cpp
LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libutils \
	libhardware \
	libcamera_metadata \
	libcamera_client \
	libsync \
	libNX_Jpeg
LOCAL_C_INCLUDES += \
	system/media/camera/include \
	system/media/core/include \
	system/core/libsync/include \
	system/core/include/utils \
	frameworks/native/include \
	frameworks/av/include \
	$(LOCAL_PATH)/../gralloc \
	$(LOCAL_PATH)/../lib_jpeg \
	external/libjpeg-turbo \
	$(call include-path-for)

include $(BUILD_SHARED_LIBRARY)
