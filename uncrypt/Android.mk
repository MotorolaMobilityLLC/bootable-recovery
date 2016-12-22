#
# Copyright (C) 2014 MediaTek Inc.
# Modification based on code covered by the mentioned copyright
# and/or permission notice(s).
#
# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CLANG := true

LOCAL_SRC_FILES := uncrypt.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_MODULE := uncrypt

LOCAL_C_INCLUDES += bootable/recovery

LOCAL_STATIC_LIBRARIES := libmtdutils libbootloader_message libbase \
                          liblog libfs_mgr libcutils libmtdutils \

LOCAL_INIT_RC := uncrypt.rc

include $(BUILD_EXECUTABLE)
