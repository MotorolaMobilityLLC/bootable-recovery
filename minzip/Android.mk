LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Hash.c \
	SysUtil.c \
	DirUtil.c \
	Inlines.c \
	Zip.c

LOCAL_C_INCLUDES := \
	external/zlib \
	external/safe-iop/include \
	external/libselinux/include

LOCAL_STATIC_LIBRARIES := libselinux

LOCAL_MODULE := libminzip
LOCAL_MULTILIB := both

LOCAL_CLANG := true

LOCAL_CFLAGS += -Werror -Wall -Wno-int-conversion

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    Hash.c \
	SysUtil.c \
	DirUtil.c \
	Inlines.c \
	Zip.c

LOCAL_C_INCLUDES += \
	external/zlib \
	external/safe-iop/include \
        external/libselinux/include

LOCAL_STATIC_LIBRARIES := libselinux

LOCAL_MODULE := libminzip

LOCAL_CFLAGS += -Wall -D_LARGEFILE64_SOURCE -D_GNU_SOURCE

include $(BUILD_HOST_STATIC_LIBRARY)
