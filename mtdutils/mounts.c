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

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#if 1 //wschen 2012-10-23
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#endif
#include <unistd.h>
#include "mounts.h"

struct MountedVolume {
    const char *device;
    const char *mount_point;
    const char *filesystem;
    const char *flags;
};

typedef struct {
    MountedVolume *volumes;
    int volumes_allocd;
    int volume_count;
} MountsState;

static MountsState g_mounts_state = {
    NULL,   // volumes
    0,      // volumes_allocd
    0       // volume_count
};

static inline void
free_volume_internals(const MountedVolume *volume, int zero)
{
    free((char *)volume->device);
    free((char *)volume->mount_point);
    free((char *)volume->filesystem);
    free((char *)volume->flags);
    if (zero) {
        memset((void *)volume, 0, sizeof(*volume));
    }
}

#define PROC_MOUNTS_FILENAME   "/proc/mounts"

int
scan_mounted_volumes()
{
    FILE* fp;
    struct mntent* mentry;

    if (g_mounts_state.volumes == NULL) {
        const int numv = 32;
        MountedVolume *volumes = malloc(numv * sizeof(*volumes));
        if (volumes == NULL) {
            errno = ENOMEM;
            return -1;
        }
        g_mounts_state.volumes = volumes;
        g_mounts_state.volumes_allocd = numv;
        memset(volumes, 0, numv * sizeof(*volumes));
    } else {
        /* Free the old volume strings.
         */
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            free_volume_internals(&g_mounts_state.volumes[i], 1);
        }
    }
    g_mounts_state.volume_count = 0;

    /* Open and read mount table entries. */
    fp = setmntent(PROC_MOUNTS_FILENAME, "r");
    if (fp == NULL) {
        return -1;
    }
    while ((mentry = getmntent(fp)) != NULL) {
        MountedVolume* v = &g_mounts_state.volumes[g_mounts_state.volume_count++];
        v->device = strdup(mentry->mnt_fsname);
        v->mount_point = strdup(mentry->mnt_dir);
        v->filesystem = strdup(mentry->mnt_type);
        v->flags = strdup(mentry->mnt_opts);
    }
    endmntent(fp);
    return 0;
}

const MountedVolume *
find_mounted_volume_by_device(const char *device)
{
    if (g_mounts_state.volumes != NULL) {
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            MountedVolume *v = &g_mounts_state.volumes[i];
            /* May be null if it was unmounted and we haven't rescanned.
             */
            if (v->device != NULL) {
                if (strcmp(v->device, device) == 0) {
                    return v;
                }
            }
        }
    }
    return NULL;
}

const MountedVolume *
find_mounted_volume_by_mount_point(const char *mount_point)
{
    if (g_mounts_state.volumes != NULL) {
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            MountedVolume *v = &g_mounts_state.volumes[i];
            /* May be null if it was unmounted and we haven't rescanned.
             */
            if (v->mount_point != NULL) {
                if (strcmp(v->mount_point, mount_point) == 0) {
                    return v;
                }
            }
        }
    }
    return NULL;
}

int
unmount_mounted_volume(const MountedVolume *volume)
{
    /* Intentionally pass NULL to umount if the caller tries
     * to unmount a volume they already unmounted using this
     * function.
     */
    sync();
    int ret = umount(volume->mount_point);
    if (ret == 0) {
        sync();
        free_volume_internals(volume, 1);
        return 0;
    }

#if 1 //wschen 2012-10-23

      else {
        DIR *dir, *dir1;
        int fd;
        int len, len1;
        char buf[1025] ="";
        char buf1[1025] ="";
        char pid[1025] ="";
        char file_path[1025] ="";
        struct dirent *ptr, *ptr1;

        printf("%s\n", strerror(errno));

        dir = opendir("/proc/");
        if (dir) {
            while ((ptr = readdir(dir)) != NULL) {
                if (ptr->d_name[0] >= '0' && ptr->d_name[0] <= '9') {
                    snprintf(pid, sizeof(pid), "/proc/%s/cmdline", ptr->d_name);
                    fd = open(pid, O_RDONLY);
                    len1 = 0;
                    if (fd != -1) {
                        len1 = read(fd, buf1, sizeof(buf1)-1);
                        close(fd);

                        snprintf(pid, sizeof(pid), "/proc/%s/fd", ptr->d_name);

                        dir1 = opendir(pid);

                        if (dir1) {
                            while ((ptr1 = readdir(dir1)) != NULL) {
                                snprintf(file_path, sizeof(file_path), "/proc/%s/fd/%s", ptr->d_name, ptr1->d_name);
                                len = readlink(file_path, buf, sizeof(buf)-1);
                                if (len != -1) {
                                    if (strstr(buf, volume->mount_point) == buf) {
                                        if (len1) {
                                            printf("process(%s):%s\n", ptr->d_name, buf1);
                                        }
                                        printf("found:%s\n", buf);
                                    }
                                }
                            }
                            closedir(dir1);
                        }
                    }
                }
            }
            closedir(dir);
        }
    }
#endif
    return ret;
}

int
remount_read_only(const MountedVolume* volume)
{
    return mount(volume->device, volume->mount_point, volume->filesystem,
                 MS_NOATIME | MS_NODEV | MS_NODIRATIME |
                 MS_RDONLY | MS_REMOUNT, 0);
}
