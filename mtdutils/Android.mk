LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c

LOCAL_MODULE := libmtdutils
LOCAL_CLANG := true

LOCAL_MULTILIB := both

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
LOCAL_CFLAGS += -DUSE_F2FS
LOCAL_SRC_FILES += \
	blkutils.c

LOCAL_C_INCLUDES += system/extras/ext4_utils system/vold
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_C_INCLUDES += system/core/fs_mgr/include
BOARD_PARTITION_BY_NAME_PATH ?= /dev/block/bootdevice/by-name
LOCAL_CFLAGS += -DPARTITION_BY_NAME_PATH="\"$(BOARD_PARTITION_BY_NAME_PATH)\""
ifneq ($(BOARD_PARTITION_BY_NAME_PATH),)
LOCAL_CFLAGS += -DBOARD_PARTITION_BY_NAME_PATH=\"$(BOARD_PARTITION_BY_NAME_PATH)\"
LOCAL_CFLAGS += -DBOARD_RECOVERY_OLD_PARTITION_BY_NAME_PATH=\"$(BOARD_RECOVERY_OLD_PARTITION_BY_NAME_PATH)\"
endif
endif

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := flash_image.c
LOCAL_MODULE := flash_image
LOCAL_MODULE_TAGS := eng
LOCAL_STATIC_LIBRARIES := libmtdutils
LOCAL_SHARED_LIBRARIES := libcutils liblog libc
include $(BUILD_EXECUTABLE)
