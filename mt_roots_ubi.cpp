/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fs_mgr.h>
#include "mtdutils/mtdutils.h"
#include "mtdutils/mounts.h"
#include "roots.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(UBIFS_SUPPORT)

#include <time.h>
#include "libubi.h"
#include "ubiutils-common.h"
#include "util.h"

#define DEFAULT_CTRL_DEV "/dev/ubi_ctrl"

//#define UBI_EC_HDR_MAGIC  0x55424923
#define UBI_EC_HDR_MAGIC  0x23494255

#if defined(UBIFS_SUPPORT) || defined (MTK_NAND_MTK_FTL_SUPPORT)

struct ubi_blkcreate_req {
    __s8  padding[128];
}  __packed;
#define UBI_VOL_IOC_MAGIC 'O'
#define UBI_IOCVOLCRBLK _IOW(UBI_VOL_IOC_MAGIC, 7, struct ubi_blkcreate_req)
#endif

int ubifs_exist(const char *part_name)
{
    const MtdPartition *partition;
    MtdReadContext *mtd_read;
    char buf[64] = {0};
    __u32 *magic;

    mtd_scan_partitions();
    partition = mtd_find_partition_by_name(part_name);
    if (partition == NULL) {
        fprintf(stderr,"1. failed to find \"%s\" partition\n", part_name);
        return 0;
    }
    mtd_read = mtd_read_partition(partition);
    if (mtd_read == NULL) {
        fprintf(stderr,"2. failed to open \"%s\" partition\n", part_name);
        return 0;
    }
    if (64 != mtd_read_data(mtd_read, buf, 64)) {
        fprintf(stderr,"3. failed to read \"%s\" partition\n", part_name);
        mtd_read_close(mtd_read);
        return 0;
    }
    mtd_read_close(mtd_read);
    magic = (__u32 *)buf;
    if (*magic == UBI_EC_HDR_MAGIC) {
        return 1;
    }

    return 0;
}

#define UBI_CTRL_DEV "/dev/ubi_ctrl"
#define UBI_SYS_PATH "/sys/class/ubi"


time_t gettime(void)
{
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        //ERROR("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        LOGE("clock_gettime(CLOCK_MONOTONIC) failed");
        return 0;
    }

    return ts.tv_sec;
}


int wait_for_file(const char *filename, int timeout)
{
    struct stat info;
    time_t timeout_time = gettime() + timeout;
    int ret = -1;

    while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
        usleep(10000);

    return ret;
}

static int ubi_dev_read_int(int dev, const char *file, int def)
{
    int fd, val = def;
    char path[128], buf[64];

    sprintf(path, UBI_SYS_PATH "/ubi%d/%s", dev, file);
    wait_for_file(path, 5);
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        return val;
    }

    if (read(fd, buf, 64) > 0) {
        val = atoi(buf);
    }

    close(fd);
    return val;
}

// Should include kernel header include/mtd/ubi-user.h

#include <linux/types.h>
#include <asm/ioctl.h>
/*
#define UBI_CTRL_IOC_MAGIC 'o'
#define UBI_IOC_MAGIC 'o'
#define UBI_VOL_NUM_AUTO (-1)
#define UBI_DEV_NUM_AUTO (-1)
#define UBI_IOCATT _IOW(UBI_CTRL_IOC_MAGIC, 64, struct ubi_attach_req)
#define UBI_IOCDET _IOW(UBI_CTRL_IOC_MAGIC, 65, __s32)
#define UBI_IOCMKVOL _IOW(UBI_IOC_MAGIC, 0, struct ubi_mkvol_req)
#define UBI_MAX_VOLUME_NAME 127
struct ubi_attach_req {
	__s32 ubi_num;
	__s32 mtd_num;
	__s32 vid_hdr_offset;
	__s8 padding[12];
};

struct ubi_mkvol_req {
	__s32 vol_id;
	__s32 alignment;
	__s64 bytes;
	__s8 vol_type;
	__s8 padding1;
	__s16 name_len;
	__s8 padding2[4];
	char name[UBI_MAX_VOLUME_NAME + 1];
} __packed;

enum {
	UBI_DYNAMIC_VOLUME = 3,
	UBI_STATIC_VOLUME  = 4,
};
*/


// Should include kernel header include/mtd/ubi-user.h
#define UBI_DEV_NUM_AUTO (-1)
#define UBI_VID_OFFSET_AUTO (0)

int ubi_attach_mtd_user(const char *mount_point)
{
    int ret;
    int vid_off;
    int ubi_ctrl=-1;
    int ubi_dev=-1;
    int vols, avail_lebs, leb_size;
    int32_t ubi_num, mtd_num, ubi_check;
    char path[128];
    char name[128];
    struct ubi_attach_req attach_req;
    struct ubi_mkvol_req mkvol_req;
    const MtdPartition *partition;
    const char* partition_name;
    int ubi_attached = 0;

    /*
       mtd_num = mtd_name_to_number(name);
       if (mtd_num == -1) {
       return -1;
       }
       */
    if (!(!strcmp(mount_point, "/system") || !strcmp(mount_point, "/data") || !strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache") || !strcmp(mount_point, "/custom"))) {
        LOGE("Invalid mount_point: %s\n", mount_point);
        return -1;
    }

#ifdef MTK_NAND_MTK_FTL_SUPPORT
    if (!strcmp(mount_point, "/system")) {
        ubi_num = 0;
        partition_name = "system";
    }

    if (!strcmp(mount_point, "/data")) {
          ubi_num = 1;
        partition_name = "userdata";
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
        ubi_num = 1;
        partition_name = "userdata";
    }
#else
    if (!strcmp(mount_point, "/system")) {
        ubi_num = 1;
        partition_name = "system";
    }

    if (!strcmp(mount_point, "/data")) {
          ubi_num = 0;
        partition_name = "userdata";
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
#ifdef MTK_SLC_BUFFER_SUPPORT
        ubi_num = 0;
        partition_name = "userdata";
#else
        ubi_num = 2;
        partition_name = "cache";
#endif
    }
#endif
    if (!strcmp(mount_point, "/custom")) {
        //ubi_num = 3;
        //mtd_num = 14;
    }

    mtd_scan_partitions();
    partition = mtd_find_partition_by_name(partition_name);
    if (partition == NULL) {
        LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                partition_name, mount_point);
        return -1;
    }

    mtd_num = mtd_part_to_number(partition);
    printf("mount point[%s], mtd_num:%d \n", mount_point, mtd_num);

    //Check if device already attached
    for (ubi_check = 0; ubi_check < 4; ubi_check++) {
        sprintf(path, "/sys/class/ubi/ubi%d/mtd_num", ubi_check);
        ubi_dev = open(path, O_RDONLY);
        if (ubi_dev != -1) {
            ret = read(ubi_dev, path, sizeof(path));
            close(ubi_dev);
            if (ret > 0 && mtd_num == atoi(path)) {
                printf("ubi%d already attached\n", ubi_check);
                ubi_attached = 1;
                //return ubi_check;
            }
        }
    }

    //If UBI device is already attached, skip it, just make UBI volume
    if (!ubi_attached) {
        ubi_ctrl = open(UBI_CTRL_DEV, O_RDONLY);
        printf("ubi_ctrl = %d\n", ubi_ctrl);

        if (ubi_ctrl == -1) {
            LOGE("failed to open UBI_CTRL_DEV\n");
            return -1;
        }

        //attach UBI device to MTD

        printf("ubi_num = %d\n",ubi_num);

        memset(&attach_req, 0, sizeof(struct ubi_attach_req));
        attach_req.ubi_num =  ubi_num;
        attach_req.mtd_num = mtd_num;
        attach_req.vid_hdr_offset = UBI_VID_OFFSET_AUTO;

        ret = ioctl(ubi_ctrl, UBI_IOCATT, &attach_req);
        if (ret == -1) {
            close(ubi_ctrl);
            LOGE("failed to UBI_IOCATT\n");
            return -1;
        }
    }
    //ubi_num = attach_req.ubi_num;
    vid_off = attach_req.vid_hdr_offset;
    vols = ubi_dev_read_int(ubi_num, "volumes_count", -1);
    if (vols == 0) {
        long long data_vol_size = 0;
        sprintf(path, "/dev/ubi%d", ubi_num);
        ubi_dev = open(path, O_RDONLY);
        if (ubi_dev == -1) {
            close(ubi_ctrl);
            LOGE("failed to open attached UBI device\n");
            return ubi_num;
        }

        avail_lebs = ubi_dev_read_int(ubi_num, "avail_eraseblocks", 0);
        leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);
        data_vol_size = (long long)avail_lebs * leb_size;
#if defined(MTK_MLC_NAND_SUPPORT)
#if defined(MTK_IPOH_SUPPORT)
        if (!strcmp(mount_point, "/data")) {
            data_vol_size -= BOARD_UBIFS_IPOH_VOLUME_SIZE;
        }
#endif
#endif
        memset(&mkvol_req, 0, sizeof(struct ubi_mkvol_req));
        mkvol_req.vol_id = UBI_VOL_NUM_AUTO;
        mkvol_req.alignment = 1;
        mkvol_req.bytes = data_vol_size;
        mkvol_req.vol_type = UBI_DYNAMIC_VOLUME;
        ret = snprintf(mkvol_req.name, UBI_MAX_VOLUME_NAME + 1, "%s", name);
        mkvol_req.name_len = ret;
        ioctl(ubi_dev, UBI_IOCMKVOL, &mkvol_req);
#if defined(MTK_MLC_NAND_SUPPORT)
#if defined(MTK_IPOH_SUPPORT)
        if (!strcmp(mount_point, "/data")) {
                memset(&mkvol_req, 0, sizeof(struct ubi_mkvol_req));
                mkvol_req.vol_id = UBI_VOL_NUM_AUTO;
                mkvol_req.alignment = 1;
                mkvol_req.bytes = (long long)BOARD_UBIFS_IPOH_VOLUME_SIZE;
                mkvol_req.vol_type = UBI_DYNAMIC_VOLUME;
                ret = snprintf(mkvol_req.name, UBI_MAX_VOLUME_NAME + 1, "%s", "ipoh");
                mkvol_req.name_len = ret;
                ioctl(ubi_dev, UBI_IOCMKVOL, &mkvol_req);
        }
#endif
#endif
        close(ubi_dev);
    }

    close(ubi_ctrl);
    return ubi_num;
}

int ubi_detach_dev(int dev)
{
    printf("It's in ubi_detach_dev!!\n");

    int ret, ubi_ctrl;
    ubi_ctrl = open(UBI_CTRL_DEV, O_RDONLY);
    if (ubi_ctrl == -1) {
        return -1;
    }

    ret = ioctl(ubi_ctrl, UBI_IOCDET, &dev);
    close(ubi_ctrl);
    return ret;
}


int ubi_mkvol_user(const char *mount_point)
{
    //int fd;
    int ret;
    int ubi_num, ubi_dev,vols;
    int avail_lebs, leb_size;
    char path[128];
    struct ubi_mkvol_req r;
    //size_t n;

    //memset(&r, 0, sizeof(struct ubi_mkvol_req));
    if (!(!strcmp(mount_point, "/system") || !strcmp(mount_point, "/data") || !strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache") || !strcmp(mount_point, "/custom"))) {
        LOGE("Invalid mount_point: %s\n", mount_point);
        return -1;
    }

    if (!strcmp(mount_point, "/system")) {
        ubi_num = 0;
    }

    if (!strcmp(mount_point, "/data")) {
        ubi_num = 1;
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
        ubi_num = 2;
    }

    if (!strcmp(mount_point, "/custom")) {
        //ubi_num = 3;
    }

    vols = ubi_dev_read_int(ubi_num, "volumes_count", -1);
    if (vols == 0) {
        sprintf(path, "/dev/ubi%d", ubi_num);
        ubi_dev = open(path, O_RDONLY);
        if (ubi_dev == -1) {
            //close(ubi_ctrl);
            LOGE("failed to open attached UBI device\n");
            return ubi_num;
        }

        avail_lebs = ubi_dev_read_int(ubi_num, "avail_eraseblocks", 0);
        leb_size = ubi_dev_read_int(ubi_num, "eraseblock_size", 0);

        //Make UBI volume
        memset(&r, 0, sizeof(struct ubi_mkvol_req));
        r.vol_id = 0; //UBI_VOL_NUM_AUTO;
        r.alignment = 1;
        r.bytes = (long long)avail_lebs * leb_size;
        r.vol_type = UBI_DYNAMIC_VOLUME;
        ret = snprintf(r.name, UBI_MAX_VOLUME_NAME + 1, "%s", mount_point);
        r.name_len = ret;
        ret = ioctl(ubi_dev, UBI_IOCMKVOL, &r);

        close(ubi_dev);

#ifdef UDEV_SETTLE_HACK
        //	if (system("udevsettle") == -1)
        //		return -1;
        usleep(100000);
#endif

        if (ret == -1) {
            LOGE("failed to make UBI volume\n");
            return ret;
        }
    }

    printf("make UBI volume success\n");
    return 0;

}

int ubi_rmvol_user(const char *mount_point)
{
    //int fd, ret;

    int ret, ubi_num, ubi_dev,vols;
    int vol_id = 0;
    char path[128];


    //memset(&r, 0, sizeof(struct ubi_mkvol_req));
    if (!(!strcmp(mount_point, "/system") || !strcmp(mount_point, "/data") || !strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache") || !strcmp(mount_point, "/custom"))) {
        LOGE("Invalid mount_point: %s\n", mount_point);
        return -1;
    }

    if (!strcmp(mount_point, "/system")) {
        ubi_num = 0;
    }

    if (!strcmp(mount_point, "/data")) {
        ubi_num = 1;
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
        ubi_num = 2;
    }

    if (!strcmp(mount_point, "/custom")) {
        //ubi_num = 3;
    }

    vols = ubi_dev_read_int(ubi_num, "volumes_count", -1);
    if (vols == 0) {
        sprintf(path, "/dev/ubi%d", ubi_num);
        ubi_dev = open(path, O_RDONLY);
        if (ubi_dev == -1) {
            //close(ubi_ctrl);
            LOGE("failed to open attached UBI device\n");
            return ubi_num;
        }


        /*
        //desc = desc;
        fd = open(ubi_dev, O_RDONLY);
        if (fd == -1)
        return -1; //sys_errmsg("cannot open \"%s\"", node);
        */

        ret = ioctl(ubi_dev, UBI_IOCRMVOL, &vol_id);
        if (ret == -1) {
            close(ubi_dev);
            LOGE("failed to remove UBI volume\n");
            return ret;
        }

        close(ubi_dev);

#ifdef UDEV_SETTLE_HACK
        //	if (system("udevsettle") == -1)
        //		return -1;
        usleep(100000);
#endif
    }

    printf("Remove UBI volume success\n");
    return 0;
}

int ubi_tlc_format(const char *mount_point) {

    printf("It's in ubi_tlc_format!!\n");
    int ret;
    int ubi_num;
    int vol_num;
    uint64_t clean_size = 0;
    char path[64];

    memset(path, 0, 64);
    if (!strcmp(mount_point, "/system")) {
      ubi_num = 1;
      vol_num = 0;
    }

    if (!strcmp(mount_point, "/data")) {
      ubi_num = 0;
      vol_num = 0;
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
      ubi_num = 0;
      vol_num = 1;
    }

    sprintf(path, "/dev/ubi%d_%d", ubi_num, vol_num);
    printf("format path=%s\n", path);
    int fd = open(path, O_RDWR);
    if(fd == -1)
    {
      printf("Error: open %s fail!\n",path);
      return -1;
    }

    ret = ioctl(fd, UBI_IOCVOLUP, &clean_size);
    LOGE("ioctl UBI_IOCVOLUP ret=%d\n", ret);
    close(fd);
    return 0;
}

int ubi_format(const char *mount_point) {
    //Detach UBI volume before formating
    printf("It's in ubi_format!!\n");
    const char* partition_name;
    char mtd_dev_name[20];
    const MtdPartition *partition;
    int32_t mtd_num;
    int32_t ubi_num = -1;

    if (!strcmp(mount_point, "/system")) {
        ubi_num = 1;
        partition_name = "system";
    }

    if (!strcmp(mount_point, "/data")) {
        ubi_num = 0;
        partition_name = "userdata";
    }

    if (!strcmp(mount_point, "/cache") || !strcmp(mount_point, "/.cache")) {
        ubi_num = 2;
        partition_name = "cache";
    }


    if (ubi_num != -1) {
        ubi_detach_dev(ubi_num);
        printf("Back to ubi_format!!\n");
    } else {
        printf("Can not find a ubi device![%s], error:%s\n", mount_point, strerror(errno));
        return -1;
    }

    //Run-time get mtd_dev_name
    mtd_scan_partitions();
    partition = mtd_find_partition_by_name(partition_name);
    if (partition == NULL) {
        LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                partition_name,mount_point);
        return -1;
    }

    mtd_num = mtd_part_to_number(partition);
    printf("mtd_num = %d\n", mtd_num);
    sprintf(mtd_dev_name, "/dev/mtd/mtd%d", mtd_num);

    printf("Formatting %s -> %s\n", mount_point, mtd_dev_name);

    const char* binary_path = "/sbin/ubiformat";
    const char* skip_questions = "-y";

    int check;
    check = chmod(binary_path, 0777);
    printf("chmod = %d\n", check);

    const char** args = (const char**)malloc(sizeof(char*) * 4);
    args[0] = binary_path;
    args[1] = (const char *)mtd_dev_name;
    args[2] = skip_questions;
    args[3] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execv(binary_path, (char* const*)args);
        fprintf(stdout, "E:Can't run %s (%s)\n", binary_path, strerror(errno));
        _exit(-1);
    }

    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in ubiformat\n(Status %d)\n", WEXITSTATUS(status));
        return -1;
    }


    //Attatch UBI device & Make UBI volume
    int n = -1;
    n = ubi_attach_mtd_user(mount_point);

    if ((n != -1) && (n < 4)) {
        printf("Try to attatch /dev/ubi%d_0... volume is attached \n", n);
    } else {
        LOGE("failed to attach /dev/ubi%d_0%s\n", n);
        return -1;
    }

    return 0;
}

#endif
#ifdef MTK_NAND_MTK_FTL_SUPPORT
int ftl_attach_ubi(int ubi_num, int vol_num)
{
    int err = 0;
    int ubi_vol;
    char tmp[25];
    int n = sprintf(tmp, "/dev/ubi%d_%d", ubi_num, vol_num);
    ubi_vol = open(tmp, O_RDWR);
    printf("%s: open path=%s, ret ubi_vol=%d\n", __func__, tmp, ubi_vol);
    err = ioctl(ubi_vol, UBI_IOCVOLCRBLK, NULL);

    if (err) {
        if (errno == EEXIST)
            printf("Block device exists\n");
        else{
            if (errno == ENOSYS)
                printf("MTK FTL is not present in the system");
            if (errno == ENOTTY)
                printf("MTK FTL not supported (check your kernel version)");
            printf("cannot create block device %d\n", err);
            goto out_close;
        }
    }

    close(ubi_vol);
    return 0;

out_close:
    close(ubi_vol);
    return -1;

}
#endif

int ensure_ubi_attach(const char* path)
{
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path /data\n");
        return -1;
    }
#if defined (UBIFS_SUPPORT)

    mkdir(v->mount_point, 0755);  // in case it doesn't already exist

    //Attatch UBI device & Make UBI volum
    int n = -1;
    n = ubi_attach_mtd_user(v->mount_point);

    if ((n != -1) && (n < 4)) {
        printf("Try to attatch %s \n", v->blk_device);
        printf("/dev/ubi%d_0 is attached \n", n);
    } else {
        LOGE("failed to attach %s\n", v->blk_device);
        return -1;
    }
#endif
    return 0;
}

#ifdef __cplusplus
}
#endif

