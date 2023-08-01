LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := test_v4l2_camera
LOCAL_SRC_FILES := test_v4l2_camera.cpp

include $(BUILD_EXECUTABLE)