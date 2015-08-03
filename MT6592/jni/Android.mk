LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS += -pie -fPIE -O0
LOCAL_LDFLAGS += -pie -fPIE

LOCAL_SRC_FILES := pwn.c

LOCAL_MODULE:= pwn

include $(BUILD_EXECUTABLE)
