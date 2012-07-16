LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := libamc
LOCAL_COPY_HEADERS := \
    ATmodemControl.h \
    AudioModemControl.h \
    amc.h
include $(BUILD_COPY_HEADERS)
include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := libamc
LOCAL_COPY_HEADERS := \
    amc.h \
    ATmodemControl.h  \
    AudioModemControl.h
include $(BUILD_COPY_HEADERS)
include $(CLEAR_VARS)

LOCAL_CFLAGS := \
        -DDEBUG

LOCAL_SRC_FILES := \
        AudioModemControl_IFX_XMM6160.c \
        AmcConfDev.c \
        ATModemControl.cpp

LOCAL_C_INCLUDES += \
        hardware/intel/rapid_ril/CORE \
        system/core/include/cutils \
        hardware/intel/IFX-modem

LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/at-parser \
        $(LOCAL_PATH)/../tty-handler \
        $(TARGET_OUT_HEADERS)/event-listener \
        $(TARGET_OUT_HEADERS)/at-manager

LOCAL_C_INCLUDES += \
        external/stlport/stlport/ \
        bionic/libstdc++ \
        bionic/

LOCAL_SHARED_LIBRARIES := libstlport libcutils libat-manager

LOCAL_MODULE := libamc
LOCAL_MODULE_TAGS := optional

TARGET_ERROR_FLAGS += -Wno-non-virtual-dtor

include $(BUILD_SHARED_LIBRARY)

