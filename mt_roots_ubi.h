/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#ifndef MT_ROOTS_UBI_H_
#define MT_ROOTS_UBI_H_

#ifdef __cplusplus
extern "C" {
#endif

int ensure_ubi_attach(const char* path);
#if defined (UBIFS_SUPPORT)
time_t gettime(void);
int wait_for_file(const char *filename, int timeout);
static int ubi_dev_read_int(int dev, const char *file, int def);
int ubi_attach_mtd_user(const char *mount_point);
int ubi_detach_dev(int dev);
int ubi_mkvol_user(const char *mount_point);
int ubi_rmvol_user(const char *mount_point);
int ubi_format(const char *mount_point);
int ubi_tlc_format(const char *mount_point);
int ubifs_exist(const char *part_name);
#endif

#ifdef __cplusplus
}
#endif

#endif

