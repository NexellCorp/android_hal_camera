LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
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
	libsync
LOCAL_C_INCLUDES += \
	system/media/camera/include \
	system/media/core/include \
	system/core/libsync/include \
	system/core/include/utils \
	frameworks/native/include \
	frameworks/av/include \
	$(LOCAL_PATH)/../gralloc \
	$(call include-path-for)

include $(BUILD_SHARED_LIBRARY)
