#
# Copyright (C) 2014 MediaTek Inc.
# Modification based on code covered by the mentioned copyright
# and/or permission notice(s).
#

MEDIATEK_RECOVERY_PATH := vendor/mediatek/proprietary/bootable/recovery

LOCAL_SRC_FILES += \
         mt_install.cpp

ifeq ($(MTK_CACHE_MERGE_SUPPORT), true)
LOCAL_CFLAGS += -DCACHE_MERGE_SUPPORT
endif

ifeq ($(TARGET_USERIMAGES_USE_UBIFS),true)
LOCAL_CFLAGS += -DUBIFS_SUPPORT
LOCAL_STATIC_LIBRARIES += ubiutils
endif


ifeq ($(MTK_SLC_BUFFER_SUPPORT), yes)
LOCAL_CFLAGS += -DMTK_SLC_BUFFER_SUPPORT
endif

LOCAL_C_INCLUDES += $(MEDIATEK_RECOVERY_PATH) $(MEDIATEK_RECOVERY_PATH)/utils/include

ifeq ($(MTK_SLC_BUFFER_SUPPORT), yes)
LOCAL_CFLAGS += -DMTK_SLC_BUFFER_SUPPORT
endif

LOCAL_C_INCLUDES += \
         $(MEDIATEK_RECOVERY_PATH) \
         $(MEDIATEK_RECOVERY_PATH)/utils/include \
         system/core/libsparse


LOCAL_STATIC_LIBRARIES += libpartition

