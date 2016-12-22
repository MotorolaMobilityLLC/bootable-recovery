#
# Copyright (C) 2014 MediaTek Inc.
# Modification based on code covered by the mentioned copyright
# and/or permission notice(s).
#
MEDIATEK_RECOVERY_PATH := vendor/mediatek/proprietary/bootable/recovery

ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), arm arm64))
    ifneq ($(MTK_BSP_PACKAGE),yes)
        WITH_BACKUP_RESTORE := true
	# SPECIAL_FACTORY_RESET will backup /data/app when do factory reset if SD is existed
	ifeq ($(MTK_SPECIAL_FACTORY_RESET),yes)
	    SPECIAL_FACTORY_RESET := true
        else
            SPECIAL_FACTORY_RESET := false
        endif
    else
        WITH_BACKUP_RESTORE := false
        SPECIAL_FACTORY_RESET := false
    endif
else
    WITH_BACKUP_RESTORE := false
endif

##########################################
# Feature option
##########################################

ifeq ($(MTK_GPT_SCHEME_SUPPORT), yes)
    WITH_GPT_SCHEME := true
else
    WITH_GPT_SCHEME := false
endif

ifeq ($(MTK_CACHE_MERGE_SUPPORT),yes)
    CACHE_MERGE_SUPPORT := true
else
    CACHE_MERGE_SUPPORT := false
endif

ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), arm arm64))
    ifneq ($(MTK_BSP_PACKAGE),yes)
        WITH_ROOT_CHECK := true
    else
        WITH_ROOT_CHECK := false
    endif
endif


##########################################
# Static library - UBIFS_SUPPORT
##########################################

ifeq ($(TARGET_USERIMAGES_USE_UBIFS),true)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := roots.cpp \
                   mt_roots.cpp \
                   mt_roots_ubi.cpp

LOCAL_MODULE := ubiutils

LOCAL_C_INCLUDES += system/extras/ext4_utils \
                    $(MEDIATEK_RECOVERY_PATH) \
                    system/core/fs_mgr/include \
                    $(MEDIATEK_RECOVERY_PATH)/utils/include

LOCAL_STATIC_LIBRARIES += libz ubi_ota_update

LOCAL_CFLAGS += -DUBIFS_SUPPORT

#add for fat merge
ifeq ($(MTK_MLC_NAND_SUPPORT),yes)
LOCAL_CFLAGS += -DBOARD_UBIFS_FAT_MERGE_VOLUME_SIZE=$(BOARD_UBIFS_FAT_MERGE_VOLUME_SIZE)
LOCAL_CFLAGS += -DBOARD_UBIFS_IPOH_VOLUME_SIZE=$(BOARD_UBIFS_IPOH_VOLUME_SIZE)
endif

LOCAL_MODULE_TAGS := eng

include $(BUILD_STATIC_LIBRARY)
endif

include $(CLEAR_VARS)
LOCAL_C_INCLUDES += \
    $(MEDIATEK_RECOVERY_PATH)/utils/include \
    external/selinux/libselinux/include \
    bionic/libc

LOCAL_SRC_FILES := \
    ../../$(MEDIATEK_RECOVERY_PATH)/utils/mt_gpt.cpp \
    ../../$(MEDIATEK_RECOVERY_PATH)/utils/mt_pmt.cpp \
    ../../$(MEDIATEK_RECOVERY_PATH)/utils/mt_partition.cpp

LOCAL_MODULE := libpartition
include $(BUILD_STATIC_LIBRARY)
