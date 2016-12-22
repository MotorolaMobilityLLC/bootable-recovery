/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "device.h"

static const char* MENU_ITEMS[] = {
    "Reboot system now",
    "Reboot to bootloader",
    "Apply update from ADB",
    "Apply update from SD card",
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
    "Apply update from sdcard2",
#endif //SUPPORT_SDCARD2
    "Wipe data/factory reset",
#ifndef AB_OTA_UPDATER
    "Wipe cache partition",
#endif  // !AB_OTA_UPDATER
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09
    "Backup user data",
    "Restore user data",
#endif
#ifdef ROOT_CHECK
    "Root integrity check",
#endif
    "Mount /system",
    "View recovery logs",
    "Run graphics test",
    "Power off",
    NULL,
};

static const Device::BuiltinAction MENU_ACTIONS[] = {
    Device::REBOOT,
    Device::REBOOT_BOOTLOADER,
    Device::APPLY_ADB_SIDELOAD,
    Device::APPLY_SDCARD,
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
    Device::APPLY_SDCARD2,
#endif // defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
    Device::WIPE_DATA,
#ifndef AB_OTA_UPDATER
    Device::WIPE_CACHE,
#endif  // !AB_OTA_UPDATER
#ifdef SUPPORT_DATA_BACKUP_RESTORE
    Device::USER_DATA_BACKUP,
    Device::USER_DATA_RESTORE,
#endif // SUPPORT_DATA_BACKUP_RESTORE
#ifdef ROOT_CHECK
    Device::CHECK_ROOT,
#endif // ROOT_CHECK
    Device::MOUNT_SYSTEM,
    Device::VIEW_RECOVERY_LOGS,
    Device::RUN_GRAPHICS_TEST,
    Device::SHUTDOWN,
};

static_assert(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]) ==
              sizeof(MENU_ACTIONS) / sizeof(MENU_ACTIONS[0]) + 1,
              "MENU_ITEMS and MENU_ACTIONS should have the same length, "
              "except for the extra NULL entry in MENU_ITEMS.");

const char* const* Device::GetMenuItems() {
  return MENU_ITEMS;
}


Device::BuiltinAction Device::InvokeMenuItem(int menu_position) {
  return menu_position < 0 ? NO_ACTION : MENU_ACTIONS[menu_position];
}

static const char* FORCE_ITEMS[] =  {"Reboot system now",
                                     "Apply sdcard:update.zip",
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
                                     "Apply sdcard2:update.zip",
#endif //SUPPORT_SDCARD2
                                     NULL };

static const Device::BuiltinAction MENU_FORCE_ACTIONS[] = {
    Device::REBOOT,
    Device::APPLY_SDCARD,
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
    Device::APPLY_SDCARD2,
#endif // defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
};


const char* const* Device::GetForceMenuItems() {
  return FORCE_ITEMS;
}

Device::BuiltinAction Device::InvokeForceMenuItem(int menu_position) {
  return menu_position < 0 ? NO_ACTION : MENU_FORCE_ACTIONS[menu_position];
}

int Device::HandleMenuKey(int key, int visible) {
  if (!visible) {
    return kNoAction;
  }

  switch (key) {
    case KEY_DOWN:
    case KEY_VOLUMEDOWN:
      return kHighlightDown;

    case KEY_UP:
    case KEY_VOLUMEUP:
      return kHighlightUp;

    case KEY_ENTER:
    case KEY_POWER:
      return kInvokeItem;

    default:
      // If you have all of the above buttons, any other buttons
      // are ignored. Otherwise, any button cycles the highlight.
      return ui_->HasThreeButtons() ? kNoAction : kHighlightDown;
  }
}
