LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := v4l2_test
LOCAL_SRC_FILES := main.cpp

include $(BUILD_EXECUTABLE)