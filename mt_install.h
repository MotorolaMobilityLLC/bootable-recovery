/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#ifndef MT_INSTALL_H_
#define MT_INSTALL_H_

#include "bootloader.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"

int mt_try_update_binary(int &ret, const char *path);
int mt_really_install_package(int &ret, const char *path, bool needs_mount, ZipArchive *zip, MemMapping *map);
int mt_really_install_package_external_modem(int &result, const char *path);

#endif

