LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := x9u_v4l2_stream_capture
LOCAL_SRC_FILES := main.cpp

include $(BUILD_EXECUTABLE)