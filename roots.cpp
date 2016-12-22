/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <errno.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#if !defined(ARCH_X86) //2012-07-10
#include <linux/mmc/sd_misc.h>
#endif
#include <time.h>
#include <fs_mgr.h>
#include "mt_roots.h"
#include "mt_roots_ubi.h"
#include "mtdutils/mtdutils.h"
#include "mtdutils/mounts.h"
#include "roots.h"
#include "common.h"
#include "mt_partition.h"
#include "make_ext4fs.h"
#include "wipe.h"

#ifdef USE_EXT4
#include "cryptfs.h"
#endif

static struct fstab *fstab = NULL;

extern struct selabel_handle *sehandle;

#if defined(CACHE_MERGE_SUPPORT)
#include <dirent.h>
#include "mt_check_partition.h"

static int need_clear_cache = 0;
static const char *DATA_CACHE_ROOT = "/data/.cache";
#endif


void load_volume_table()
{
    int i;
    int ret;

    fstab = fs_mgr_read_fstab("/etc/recovery.fstab");
    if (!fstab) {
        LOGE("failed to read /etc/recovery.fstab\n");
        LOGE("try read other fstab\n");
        fstab = mt_read_fstab();
        if (!fstab) {
            LOGE("failed to read fstab\n");
            return;
        }
    }

    ret = fs_mgr_add_entry(fstab, "/tmp", "ramdisk", "ramdisk");
    if (ret < 0 ) {
        LOGE("failed to add /tmp entry to fstab\n");
        fs_mgr_free_fstab(fstab);
        fstab = NULL;
        return;
    }

    ret = mt_load_volume_table(fstab);
    if (ret < 0 ) {
        LOGE("mt_load_volume_table fail to add entry to fstab\n");
        fs_mgr_free_fstab(fstab);
        fstab = NULL;
        return;
    }

    mt_ensure_dev_ready("/misc");
    mt_ensure_dev_ready("/cache");
    mt_fstab_translation_NAND(fstab);

    printf("recovery filesystem table\n");
    printf("=========================\n");
    for (i = 0; i < fstab->num_entries; ++i) {
        Volume* v = &fstab->recs[i];
        printf("  %d %s %s %s %lld\n", i, v->mount_point, v->fs_type,
               v->blk_device, v->length);
    }
    printf("\n");
}

Volume* volume_for_path(const char* path) {
  return fs_mgr_get_entry_for_mount_point(fstab, path);
}

// Mount the volume specified by path at the given mount_point.
int ensure_path_mounted_at(const char* path, const char* mount_point) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 0;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    if (!mount_point) {
        mount_point = v->mount_point;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(mount_point);
    if (mv) {
#if defined(CACHE_MERGE_SUPPORT)
        if (strncmp(path, "/cache", 6) == 0) {
            if (symlink(DATA_CACHE_ROOT, "/cache")) {
                if (errno != EEXIST) {
                    LOGE("create symlink from %s to %s failed(%s)\n",
                            DATA_CACHE_ROOT, "/cache", strerror(errno));
                    return -1;
                }
            }
        }
#endif
        // volume is already mounted
        return 0;
    }

    mkdir(mount_point, 0755);  // in case it doesn't already exist

#if defined (UBIFS_SUPPORT)
    if (strcmp(v->fs_type, "ubifs") == 0) {

        printf("Trying to mount %s \n", v->mount_point);

        //Attatch UBI device & Make UBI volum
        int n = -1;
        n = ubi_attach_mtd_user(v->mount_point);

        if ((n != -1) && (n < 4)) {
            printf("Try to attatch %s \n", v->blk_device);
            printf("%s is attached \n", v->blk_device);
        } else {
            LOGE("failed to attach %s\n", v->blk_device);
        }


        //Mount UBI volume
        const unsigned long flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
        char tmp[64];
#ifdef MTK_SLC_BUFFER_SUPPORT
    if(strcmp(v->mount_point, "/cache") == 0)
        strcpy(tmp, "/dev/ubi0_1");
    else if(strcmp(v->mount_point, "/data") == 0)
        strcpy(tmp, "/dev/ubi0_0");
    else if(strcmp(v->mount_point, "/system") == 0)
        strcpy(tmp, "/dev/ubi1_0");
#else
    sprintf(tmp, "/dev/ubi%d_0", n);
#endif
        wait_for_file(tmp, 5);
        result = mount(tmp, v->mount_point, v->fs_type, flags, "");
        if (result < 0) {
            printf("mount %s to %s fail\n",tmp,v->mount_point);
            ubi_detach_dev(n);
            return -1;
        } else if (result == 0) {
            printf("mount %s to %s done\n",tmp,v->mount_point);
            goto mount_done;  //Volume  successfully  mounted
        }

    }
#endif

    if (strcmp(v->fs_type, "yaffs2") == 0) {
        // mount an MTD partition as a YAFFS2 filesystem.
        mtd_scan_partitions();
        const MtdPartition* partition;
        partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                 v->blk_device, mount_point);
            return -1;
        }
        return mtd_mount_partition(partition, mount_point, v->fs_type, 0);
    } else if (strcmp(v->fs_type, "ext4") == 0 ||
               strcmp(v->fs_type, "squashfs") == 0 ||
               strcmp(v->fs_type, "vfat") == 0) {
#ifdef MTK_NAND_MTK_FTL_SUPPORT
        if (strstr(v->blk_device, "ftl") ){
            char tmp[30];
            int err = 0;
            int n = -1;
            int ubi_num = v->blk_device[21] - '0';
            int vol_num = v->blk_device[23] - '0';
            printf("debug : mtk_ftl_blk %s ubi_num %d mount_porint=%s\n", \
            v->blk_device, ubi_num, v->mount_point);
            if(strstr(v->mount_point, "system")){
                n = ubi_attach_mtd_user("/system");
            }else if(strstr(v->mount_point, "data")){
                n = ubi_attach_mtd_user("/data");
            }else if(strstr(v->mount_point, "cache")){
                n = ubi_attach_mtd_user("/cache");
            }
            if((n != ubi_num) && (n >= 0))
            {
                printf("ubi number: %d == %d\n", n, ubi_num);
                ubi_num = n;
            }
            n = sprintf(tmp, "/dev/ubi%d_%d", ubi_num, vol_num);
            if (v->fs_mgr_flags & MF_WAIT) {
                int ret = wait_for_file(tmp, WAIT_TIMEOUT);
                printf("wait_for_file(%s) ret = %d, errno = %s\n", v->blk_device, ret, strerror(errno));
            }
            err = ftl_attach_ubi(ubi_num, vol_num);
            if (err < 0) {
                printf("ftl attach ubi fail, ret=%d\n", err);
                return -1;
            }
        }
#endif

        mt_ensure_dev_ready(mount_point);
        result = mount(v->blk_device, mount_point, v->fs_type,
                       v->flags, v->fs_options);
        if (result == 0) {
            goto mount_done;
        } else {
            result = mt_ensure_path_mounted(v);
            if (result == 0)
                goto mount_done;
        }
        LOGE("failed to mount %s (%s)\n", v->mount_point, strerror(errno));
        return -1;
    }

    LOGE("unknown fs_type \"%s\" for %s\n", v->fs_type, mount_point);
    return -1;

mount_done:
#if defined(CACHE_MERGE_SUPPORT)
    if (strcmp(v->mount_point, "/data") == 0) {
        if (mkdir(DATA_CACHE_ROOT, 0770)) {
            if (errno != EEXIST) {
                LOGE("mkdir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                return -1;
            } else if (need_clear_cache) {
                LOGI("cache exists, clear it...\n");
                if (remove_dir(DATA_CACHE_ROOT)) {
                    LOGE("remove_dir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                    return -1;
                }
                if (mkdir(DATA_CACHE_ROOT, 0770) != 0) {
                    LOGE("mkdir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                    return -1;
                }
            }
        }
        if (symlink(DATA_CACHE_ROOT, "/cache")) {
            if (errno != EEXIST) {
                LOGE("create symlink from %s to %s failed(%s)\n",
                        DATA_CACHE_ROOT, "/cache", strerror(errno));
                return -1;
            }
        }
        need_clear_cache = 0;
    }
#endif
    return 0;
}

int ensure_path_mounted(const char* path) {
    // Mount at the default mount point.
    return ensure_path_mounted_at(path, nullptr);
}

int ensure_path_unmounted(const char* path) {
    Volume* v = volume_for_path(path);

#if defined(CACHE_MERGE_SUPPORT)
    if (strncmp(path, "/cache", 6) == 0) {
        unlink(path);
        return 0;
    }
#endif

    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted; you can't unmount it.
        return -1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        // volume is already unmounted
        return 0;
    }

    return unmount_mounted_volume(mv);
}

static int exec_cmd(const char* path, char* const argv[]) {
    int status;
    pid_t child;
    if ((child = vfork()) == 0) {
        execv(path, argv);
        _exit(-1);
    }
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("%s failed with status %d\n", path, WEXITSTATUS(status));
    }
    return WEXITSTATUS(status);
}

int format_volume(const char* volume, const char* directory) {

    time_t start, end;
    start = time((time_t *)NULL);
    printf("format %s start=%u\n", volume, (unsigned int)start);

#if defined(CACHE_MERGE_SUPPORT)
    char *target_volume = (char *)volume;

    if (strcmp(target_volume, "/cache") == 0) {
        // we cannot mount data since partition size changed
        // clear cache folder when data mounted
        if (part_size_changed) {
            LOGI("partition size changed, clear cache folder when data mounted...\n");
            need_clear_cache = 1;

            // change format volume name to format actual cache partition
            target_volume = "/.cache";
        } else {
            // clear DATA_CACHE_ROOT
            if (ensure_path_mounted(DATA_CACHE_ROOT) != 0) {
                LOGE("Can't mount %s while clearing cache!\n", DATA_CACHE_ROOT);
                return -1;
            }
            if (remove_dir(DATA_CACHE_ROOT)) {
                LOGE("remove_dir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                return -1;
            }
            if (mkdir(DATA_CACHE_ROOT, 0770) != 0) {
                LOGE("Can't mkdir %s (%s)\n", DATA_CACHE_ROOT, strerror(errno));
                return -1;
            }
            LOGI("format cache successfully!\n");

            end = time((time_t *)NULL);
            printf("format end=%u duration=%u\n", (unsigned int)end, (unsigned int)(end - start));
            return 0;
        }
    }

    Volume* v = volume_for_path(target_volume);
    if (v == NULL) {
        LOGE("unknown volume \"%s\"\n", target_volume);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", target_volume);
        return -1;
    }
    if (strcmp(v->mount_point, target_volume) != 0) {
        LOGE("can't give path \"%s\" to format_volume\n", target_volume);
        return -1;
    }

    if (ensure_path_unmounted(target_volume) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }
#else
    Volume* v = volume_for_path(volume);
    if (v == NULL) {
        LOGE("unknown volume \"%s\"\n", volume);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", volume);
        return -1;
    }
    if (strcmp(v->mount_point, volume) != 0) {
        LOGE("can't give path \"%s\" to format_volume\n", volume);
        return -1;
    }

    if (ensure_path_unmounted(volume) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }
#endif

#if defined (UBIFS_SUPPORT)
    if (strcmp(v->fs_type, "ubifs") == 0) {

        int ret;
#ifdef MTK_SLC_BUFFER_SUPPORT
        ret = ubi_tlc_format(v->mount_point);
#else
        ret = ubi_format(v->mount_point);
#endif

        if (!ret) {
            end = time((time_t *)NULL);
            printf("format end=%u duration=%u\n", (unsigned int)end, (unsigned int)(end - start));
            return 0;
        } else {
            LOGE("Ubiformat failed on \"%s\"\n", v->mount_point);
        }


#if 0

        int ret;
        //Remove volume
        if(ubi_rmvol_user(v->mount_point)!=0){
            LOGE("failed to remove %s\n", v->blk_device);
            return -1;
        }

        //Make volume
        ret = ubi_mkvol_user(v->mount_point);
        if(!ret){
            printf("%s volume made\n", v->blk_device);
            return 0;
        }
#endif
    }
#endif

    if (strcmp(v->fs_type, "yaffs2") == 0 || strcmp(v->fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", v->blk_device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", v->blk_device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", v->blk_device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n", v->blk_device);
            return -1;
        }

        end = time((time_t *)NULL);
        printf("format end=%u duration=%u\n", (unsigned int)end, (unsigned int)(end - start));
        return 0;
    }

    if (strcmp(v->fs_type, "ext4") == 0 || strcmp(v->fs_type, "f2fs") == 0) {
        if (!mt_is_support_gpt()) {
#if !defined(ARCH_X86) //2012-07-10
            int fd;
            struct msdc_ioctl msdc_io;

            fd = open("/dev/misc-sd", O_RDONLY);
            if (fd < 0) {
                LOGE("open: /dev/misc-sd failed\n");
                return -1;
            }

            msdc_io.opcode = MSDC_ERASE_PARTITION;
#if defined(CACHE_MERGE_SUPPORT)
            if (!strcmp(target_volume, "/.cache")) {
                msdc_io.buffer = (unsigned int*) "cache";
                msdc_io.total_size = 6;
            } else if (!strcmp(target_volume, "/data")) {
                msdc_io.buffer = (unsigned int*) "usrdata";
                msdc_io.total_size = 8;
            }
#else
            if (!strcmp(volume, "/cache")) {
                msdc_io.buffer = (unsigned int*) "cache";
                msdc_io.total_size = 6;
            } else if (!strcmp(volume, "/data")) {
                msdc_io.buffer = (unsigned int*) "usrdata";
                msdc_io.total_size = 8;
            }
#endif
            ioctl(fd, 0, &msdc_io);
            close(fd);
#endif
        } // end of not support_gpt

        // if there's a key_loc that looks like a path, it should be a
        // block device for storing encryption metadata.  wipe it too.
        if (v->key_loc != NULL && v->key_loc[0] == '/') {
            LOGI("wiping %s\n", v->key_loc);
            int fd = open(v->key_loc, O_WRONLY | O_CREAT, 0644);
            if (fd < 0) {
                LOGE("format_volume: failed to open %s\n", v->key_loc);
                return -1;
            }
            wipe_block_device(fd, get_file_size(fd));
            close(fd);
        }

#ifndef CRYPT_FOOTER_OFFSET
#define CRYPT_FOOTER_OFFSET (0)
#endif

#ifdef MTK_NAND_MTK_FTL_SUPPORT
        if (strstr(v->blk_device, "ftl") ){
            char tmp[30];
            int err = 0;
            int n = -1;
            int ubi_num = v->blk_device[21] - '0';
            int vol_num = v->blk_device[23] - '0';
            printf("format debug : mtk_ftl_blk %s ubi_num %d mount_porint=%s\n", \
                v->blk_device, ubi_num, v->mount_point);
            if(strstr(v->mount_point, "system")){
                n = ubi_attach_mtd_user("/system");
            }else if(strstr(v->mount_point, "data")){
                n = ubi_attach_mtd_user("/data");
            }else if(strstr(v->mount_point, "cache")){
                n = ubi_attach_mtd_user("/cache");
            }
            if((n != ubi_num) && (n >= 0))
            {
                printf("ubi number: %d == %d\n", n, ubi_num);
                ubi_num = n;
            }
            n = sprintf(tmp, "/dev/ubi%d_%d", ubi_num, vol_num);
            if (v->fs_mgr_flags & MF_WAIT) {
                int ret = wait_for_file(tmp, WAIT_TIMEOUT);
                printf("wait_for_file(%s) ret = %d, errno = %s\n", v->blk_device, ret, strerror(errno));
            }
            err = ftl_attach_ubi(ubi_num, vol_num);
            if (err < 0) {
                printf("ftl attach ubi fail, ret=%d\n", err);
                return -1;
            }

        }
#endif
        ssize_t length = 0;
        if (v->length != 0) {
            length = v->length;
        } else if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0) {
            length = -CRYPT_FOOTER_OFFSET;
        }

        int result = 0;
        if (strcmp(v->fs_type, "ext4") == 0) {

#if defined(CACHE_MERGE_SUPPORT)
            result = make_ext4fs_directory(v->blk_device, length, target_volume, sehandle, directory);
#else
            LOGE("Before make_ext4fs_directory v->blk_device:%s length:%zd volume=%s directory=%s\n"
            , v->blk_device, length, volume, directory);
            result = make_ext4fs_directory(v->blk_device, length, volume, sehandle, directory);
#endif

        } else {   /* Has to be f2fs because we checked earlier. */
            if (v->key_loc != NULL && strcmp(v->key_loc, "footer") == 0 && length < 0) {
                LOGE("format_volume: crypt footer + negative length (%zd) not supported on %s\n", length, v->fs_type);
                return -1;
            }
            if (length < 0) {
                LOGE("format_volume: negative length (%zd) not supported on %s\n", length, v->fs_type);
                return -1;
            }
            char *num_sectors;
            if (asprintf(&num_sectors, "%zd", length / 512) <= 0) {
                LOGE("format_volume: failed to create %s command for %s\n", v->fs_type, v->blk_device);
                return -1;
            }
            const char *f2fs_path = "/sbin/mkfs.f2fs";
            const char* const f2fs_argv[] = {"mkfs.f2fs", "-t", "-d1", v->blk_device, num_sectors, NULL};

            result = exec_cmd(f2fs_path, (char* const*)f2fs_argv);
            free(num_sectors);
        }

        if (result != 0) {
            LOGE("format_volume: make %s failed on %s with %d(%s)\n", v->fs_type, v->blk_device, result, strerror(errno));
            return -1;
        }

        end = time((time_t *)NULL);
        printf("format end=%u duration=%u\n", (unsigned int)end, (unsigned int)(end - start));
        return 0;
    }

    LOGE("format_volume: fs_type \"%s\" unsupported\n", v->fs_type);
    return -1;
}

int format_volume(const char* volume) {
    return format_volume(volume, NULL);
}

int setup_install_mounts() {
    if (fstab == NULL) {
        LOGE("can't set up install mounts: no fstab loaded\n");
        return -1;
    }
    for (int i = 0; i < fstab->num_entries; ++i) {
        Volume* v = fstab->recs + i;

        if (strcmp(v->mount_point, "/tmp") == 0 ||
            strcmp(v->mount_point, "/cache") == 0) {
            if (ensure_path_mounted(v->mount_point) != 0) {
                LOGE("failed to mount %s\n", v->mount_point);
                return -1;
            }

        } else {
            if (ensure_path_unmounted(v->mount_point) != 0) {
                LOGE("failed to unmount %s\n", v->mount_point);
                return -1;
            }
        }
    }
    return 0;
}

