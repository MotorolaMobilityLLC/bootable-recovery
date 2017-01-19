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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h> // for basename
#include <linux/fs.h>
#include <libgen.h>
#include <sys/mount.h>  // for _IOW, _IOR, mount()
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <fs_mgr.h>
#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include "blkutils.h"
#include "cryptfs.h"
#include "common.h"

#define BUFFSIZE 512
#define DEVICE_NAME_MAX_LENGTH 64

#ifndef BLKDISCARD
#define BLKDISCARD _IO(0x12,119)
#endif
#define ERASE_SIZE_MIN      (4  * 1024 * 1024)
#define ERASE_SIZE_MAX      (16 * 1024 * 1024)

#define BLKUTILS_NUM_BLKS_FOR_HAB_CSF 3
#define READ_SIZE (4*1024*1024)

struct BlkPartition {
    char *device_name;
    uint64_t size;
    uint64_t block_count;
    unsigned int io_buffer_size;
    unsigned int erase_size_min;
    unsigned int erase_size_max;
    char *name;
};


typedef struct {
    BlkPartition *partitions;
    int partitions_allocd;
    int partition_count;
} BlkState;

static BlkState g_blk_state = {
    NULL,   // partitions
    0,      // partitions_allocd
    -1      // partition_count
};

// size of string output buffers that will be passed to generate_device_name()
#define BLKUTILS_DEVICE_NAME_MAX_LENGTH 64
#define BLOCKUTILS_NUM_BLKS_FOR_HAB_CSF    3
#define MAX_MMC_PARTITIONS 64
#define BUF_SIZE MAX_MMC_PARTITIONS*128

int decimal_binary(int n);
int power(int base, int exp);

int write_to_file(int fd, unsigned char *data, size_t length) {
    ssize_t just_written = 0;
    size_t total_written = 0;
    int ret = 0;

    while (total_written < length) {

        just_written = write(fd, data + total_written, length - total_written);

        if (just_written < 0) {
            fprintf(stderr, "write failure. wrote %x out of %x errno %d (%s)\n",
                    total_written, length, errno, strerror(errno));
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            goto EXIT_ON_ERROR;
        }

        total_written += just_written;
    }

    if (total_written != length) {
        fprintf(stderr, "Unable to write the entire data wrote %x out of %x errno %d (%s)\n",
                total_written, length, errno, strerror(errno));
        goto EXIT_ON_ERROR;
    }

EXIT:
    //printf("write_to_file wrote %x bytes out of %x\n", total_written, length);
    return (ret);

EXIT_ON_ERROR:
    ret = -1;
    fprintf(stderr, "Failure in function write_to_file\n");
    goto EXIT;
}

int read_from_file(int fd, unsigned char *data, size_t length) {
    int ret = 0;
    ssize_t just_read = 0;
    size_t total_read = 0;

    while (total_read < length) {

        just_read = read(fd, data + total_read, length - total_read);

        if (just_read < 0) {
            fprintf(stderr, "read failure.  read %x out of %x errno %d (%s)\n",
                    total_read, length, errno, strerror(errno));
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            goto EXIT_ON_ERROR;
        }

        total_read += just_read;
    }

    if (total_read != length) {
        fprintf(stderr, "Unable to read the entire data read %x out of %x errno %d (%s)\n",
                       total_read, length, errno, strerror(errno));
        goto EXIT_ON_ERROR;
    }

EXIT:
    //printf("read_from_file read %x bytes out of %x\n", total_read, length);
    return (ret);

EXIT_ON_ERROR:
    ret = -1;
    fprintf(stderr, "Failure in function read_from_file\n");
    goto EXIT;
}

static int
parse_partitions(BlkPartition *partitions){

    int fd = open("/proc/partitions", O_RDONLY);
    if (fd < 0)
        return -1;

    char buf[BUF_SIZE];
    buf[sizeof(buf) - 1] = '\0';
    ssize_t data_size = read(fd, buf, sizeof(buf) - 1);
    if (data_size > 0) {
        char *data_ptr = buf;
        char *data_end = buf + data_size;
        while (data_ptr < data_end) {
            int dev_major, dev_minor;
            unsigned long long blocks_num;
            char dev_name[BLKUTILS_DEVICE_NAME_MAX_LENGTH] = {};
            char dev_alias[BLKUTILS_DEVICE_NAME_MAX_LENGTH] = {};
            if (sscanf(data_ptr, "%4d  %7d %10llu %63s%*['\t']%63[^'\n']\n",
                       &dev_major, &dev_minor, &blocks_num, dev_name, dev_alias) >= 4) {
                if (g_blk_state.partition_count < g_blk_state.partitions_allocd) {
                    BlkPartition *partition = partitions + g_blk_state.partition_count;
                    partition->name = strdup(dev_alias);
                    partition->device_name = strdup(dev_name);
                    partition->block_count = blocks_num;
                    partition->size = blocks_num * 1024;
                    partition->erase_size_min = 512 * 1024;
                    partition->erase_size_max = 16 * 1024 * 1024;
                    if (partition->size < partition->erase_size_min) {
                        partition->io_buffer_size = partition->size;
                    } else {
                        partition->io_buffer_size = partition->erase_size_min;
                    }
                    if (partition->size % partition->io_buffer_size != 0) {
                        fprintf(stderr, "blk: partition \"%s\" size %llu not a multiple of io_buffer_size %u\n",
                            partition->name, partition->size, partition->io_buffer_size);
                    }
                    g_blk_state.partition_count++;
                } else {
                    fprintf(stderr, "blk: exceeded partition definition list size of %d\n",
                        g_blk_state.partitions_allocd);
                }
            }

            /* Advance to next line */
            while (data_ptr < data_end && *data_ptr != '\n') data_ptr++;
            while (data_ptr < data_end && *data_ptr == '\n') data_ptr++;
        }
    }

    close(fd);
    return 0;
}

int
block_scan_partitions()
{
    int i;
    if (g_blk_state.partitions == NULL) {
        const int nump = MAX_MMC_PARTITIONS;
        BlkPartition *partitions = malloc(nump * sizeof(*partitions));
        if (partitions == NULL) {
            errno = ENOMEM;
            return -1;
        }
        g_blk_state.partitions = partitions;
        g_blk_state.partitions_allocd = nump;
        memset(partitions, 0, nump * sizeof(*partitions));
    }
    g_blk_state.partition_count = 0;

    /* Initialize all of the entries to make things easier later.
     * (Lets us handle sparsely-numbered partitions, which
     * may not even be possible.)
     */
    for (i = 0; i < g_blk_state.partitions_allocd; i++) {
        BlkPartition *p = &g_blk_state.partitions[i];
        if (p->name != NULL) {
            free(p->name);
            p->name = NULL;
        }
        if (p->device_name != NULL) {
            free(p->device_name);
            p->device_name = NULL;
        }
    }
#ifdef BOARD_PARTITION_BY_NAME_PATH
    const char *by_name = BOARD_PARTITION_BY_NAME_PATH;
    char link[PATH_MAX + 1];
    char device[PATH_MAX + 1];

    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int length = 0;

    if (parse_partitions(g_blk_state.partitions) != 0) {
        return -1;
    }

    dir = opendir(by_name);
    if (dir == NULL){
        by_name = BOARD_RECOVERY_OLD_PARTITION_BY_NAME_PATH;
        dir = opendir(by_name);
        if (dir == NULL) {
            return -1;
        }
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        snprintf(link, PATH_MAX, "%s/%s", by_name, entry->d_name);
        length = readlink(link, device, PATH_MAX);
        if (length <= 0)
            continue;
        device[length]=0;

        for (i = 0; i<g_blk_state.partition_count; i++) {
            BlkPartition *partition = NULL;
            partition = g_blk_state.partitions + i;

            if (strcmp(partition->device_name,basename(device)) == 0) {
                if(partition->name)
                    free(partition->name);

                partition->name = strdup(entry->d_name);
            }
        }

    }
#elif  TARGET_RECOVERY_PARTITIONS_FILE
    #include "recovery_partitions.inc"
    int num_partitions = sizeof(partition_info)/sizeof(struct generated_partition_info);
    int index;
    for(index = 0; index< num_partitions; index++){
        if(g_blk_state.partition_count < g_blk_state.partitions_allocd){
            BlkPartition *partition = g_blk_state.partitions + g_blk_state.partition_count;
            partition->device_name  = strdup(partition_info[index].device_name);
            partition->block_count = partition_info[index].block_num;
            partition->size = partition_info[index].block_num * 1024;
            partition->erase_size_min = 512 * 1024;
            partition->erase_size_max = 16  * 1024 * 1024;
            partition->name = strdup(partition_info[index].device_alias);

            if (partition->size < partition->erase_size_min) {
                partition->io_buffer_size = partition->size;
            } else {
                partition->io_buffer_size = partition->erase_size_min;
            }
            if (partition->size % partition->io_buffer_size != 0) {
                fprintf(stderr, "blk: partition \"%s\" size %llu not a multiple of io_buffer_size %u\n",
                    partition->name, partition->size, partition->io_buffer_size);
            }
            g_blk_state.partition_count++;
        }
    }
#else
    if (parse_partitions(g_blk_state.partitions) != 0 )
        return -1;
#endif

    return g_blk_state.partition_count;
}

const BlkPartition *
block_find_partition_by_name(const char *name)
{
    if (g_blk_state.partitions != NULL) {
        int i;
        for (i = 0; i < g_blk_state.partitions_allocd; i++) {
            BlkPartition *p = &g_blk_state.partitions[i];
            if (p->device_name != NULL && p->name != NULL) {
                if (strcmp(p->name, name) == 0) {
                    return p;
                }
            }
        }
    }
    return NULL;
}

int mount_data_partition()
{
    block_scan_partitions();
    int result = 0;
    char partition_name[PATH_MAX];
    char *mount_point = "/data";

    getDeviceName("userdata",partition_name,sizeof(partition_name));

    // Try first for f2fs type.
    if (mount(partition_name, mount_point, "f2fs",
                  MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
        result = -1;
    }

    if (result == -1) { // Try once for ext4
        // Attempting to mount data partition as ext4
        if (mount(partition_name, mount_point, "ext4",
                  MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
            result = -1;
        }
        else {
            result = 0;
        }
    }

    return result;
}

int umount_data_partition() {
    char *mount_point = "/data";
    int result = 0;

    if (umount(mount_point) < 0) {
        fprintf(stderr, "Unable to umount %s error %d(%s)\n", mount_point, errno, strerror(errno));
        result = -1;
    }

    return result;
}

bool doValidateErase() {
    char prod_model[PATH_MAX];
    char prod_device[PATH_MAX];
    char file_name[PATH_MAX];
    enum ValidationStatus { DO_NOT_KNOW = -1, DO_NOT_VALIDATE, VALIDATE};
    static enum ValidationStatus validate = DO_NOT_KNOW;

    property_get("ro.product.model",prod_model,"");
    property_get("ro.product.device",prod_device,"");

    if (validate == DO_NOT_KNOW) {
        if (mount_data_partition() != 0) {
            sprintf(file_name,"/cache/fdr_validation_%s_%s",prod_model,prod_device);
            if (access(file_name,F_OK) == 0) {
                validate = VALIDATE;
            } else {
                validate = DO_NOT_VALIDATE;
            }
        } else {
            sprintf(file_name,"/data/media/0/fdr_validation_%s_%s",prod_model,prod_device);
            if (access(file_name,F_OK) == 0) {
                validate = VALIDATE;
            } else {
                sprintf(file_name,"/cache/fdr_validation_%s_%s",prod_model,prod_device);
                if (access(file_name,F_OK) == 0) {
                    validate = VALIDATE;
                } else {
                    validate = DO_NOT_VALIDATE;
                }
            }
            if (umount_data_partition() < 0) {
                fprintf(stderr, "Can't umount userdata partition for validating erase\n");
            }
        }
    }

    return validate;
}
int formatRawPtn(const char *location) {
    block_scan_partitions();
    const BlkPartition* blk = block_find_partition_by_name(basename(location));
    if (blk == NULL) {
        fprintf(stderr, "no mtd partition named \"%s\"",
                location);
        return -1;
    }
    uint64_t format_size = blk->size;

    char *buffer = malloc(BUFFSIZE);
    memset(buffer, 0x00, BUFFSIZE);


    fprintf(stderr, "devname = %s\n",location);

    int fd = open(location,O_WRONLY);
    if(fd < 0){
       fprintf(stderr,"Unable to open partition %s\n", location);
       return -1;
    }

    while(format_size >= BUFFSIZE){
        if (write(fd, buffer, BUFFSIZE) != BUFFSIZE) {
            fprintf(stderr, "Error writing to partition %s\n", blk->name);
            close(fd);
            return -1;
        }
        format_size = format_size - BUFFSIZE;
    }

    if(format_size != 0){
        if(write(fd, buffer, format_size) != format_size) {
            fprintf(stderr, "Error writing to partition %s\n", blk->name);
            close(fd);
            return -1;
        }
    }

    fprintf(stderr,"%s partition Erased\n", blk->name);
    close(fd);

    return 0;
}

/* Build the argument list passed to mke2fs without danger of overflow */
#define MKE2FS_MAX_ARGS 16
#define APPEND_ARGV(ARG) if(argc < MKE2FS_MAX_ARGS) argv[argc++] = ARG

/* List of all potential paths to the mke2fs executable. */
const char* const mke2fs_paths[] = {"/sbin/mke2fs_static", "/system/bin/mke2fs"};
int mke2fs_num_paths = sizeof(mke2fs_paths) / sizeof(mke2fs_paths[0]);

int blk_format_ext2(const char* device, const char* label, int use_journal, uint64_t block_count) {
    const char *argv[MKE2FS_MAX_ARGS] = {"mke2fs"};
    char blk_count_string[10];
    int argc = 1;

    // do not reserve space for superuser
    APPEND_ARGV("-m");
    APPEND_ARGV("0");

    // use a journal if the caller requests one
    if (use_journal) {
        APPEND_ARGV("-j");
    }

    // if the caller supplied a label, set it on the file system
    if (label) {
        APPEND_ARGV("-L");
        APPEND_ARGV(label);
    }

    // specify the device to format
    APPEND_ARGV(device);

    memset(blk_count_string, 0, sizeof(blk_count_string));
    snprintf(blk_count_string, sizeof(blk_count_string), "%llu", block_count);
    APPEND_ARGV(blk_count_string);

    // print an error if the argv buffer overflows
    if(argc >= MKE2FS_MAX_ARGS) {
        fprintf(stderr, "blk_format_ext2: too many args for mke2fs: max %d\n",
            MKE2FS_MAX_ARGS-1);
        return -1;
    }

    // log the command line for debugging
    int i;
    for (i = 0; i < argc; i++)
        fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");

    // argument list must be terminated with NULL for execv
    argv[argc] = NULL;

    // fork since execv() replaces the current process
    int child_pid = fork();
    if (child_pid == 0) {
        // child process, exec mke2fs
        for (i = 0; i < mke2fs_num_paths; i++) {
            int result = execv(mke2fs_paths[i], (char* const*)argv);
            fprintf(stderr, "blk_format_ext2: failed to exec \"%s\": error %d\n",
                mke2fs_paths[i], result);
        }
        fprintf(stderr, "blk_format_ext2: all attempts to exec mke2fs failed\n");
        exit(-1);
    } else if (child_pid > 0) {
        // parent process, child started successfully
        int child_status = 0, result = -1;
        if (waitpid(child_pid, &child_status, 0) != child_pid) {
            fprintf(stderr, "blk_format_ext2: waitpid failed\n");
        } else if (WIFEXITED(child_status)) {
            int exit_status = WEXITSTATUS(child_status);
            if (exit_status != 0) {
                fprintf(stderr, "blk_format_ext2: mke2fs returned %d\n", exit_status);
            } else {
                // child exited with no error
                result = 0;
            }
        } else if (WIFSIGNALED(child_status)) {
            int signal = WTERMSIG(child_status);
            fprintf(stderr, "blk_format_ext2: mke2fs terminated with signal %d\n", signal);
        } else {
            fprintf(stderr, "blk_format_ext2: mke2fs status unknown: %d\n", child_status);
        }
        return result;
    } else {
        fprintf(stderr, "blk_format_ext2: fork failed\n");
        return -1;
    }
}

void getDeviceName(const char *name, char *device_name, int device_length) {

    int n = snprintf(device_name, device_length,"%s/%s",BOARD_PARTITION_BY_NAME_PATH,name);
}

static uint8_t get_size_in_gb(uint64_t mmc_size) {

    uint8_t bit = 1;
    for (bit = 30; bit < 64; bit++) {
        uint64_t size = 1ull << bit;
        if (size >= mmc_size)
            return size >> 30;
    }

    return 0; // error
}


uint8_t blkutils_device_size(void) {

    const BlkPartition *partition = NULL;
    int i = 0;

    if ((partition = block_find_partition_by_name("system")) == NULL) {
        return 0;
    }

    char *emmc_raw_device = strdup("mmcblkX");
    strncpy(emmc_raw_device, partition->device_name, strlen(emmc_raw_device));

    for (i = 0; i < g_blk_state.partitions_allocd; i++) {
        partition = &g_blk_state.partitions[i];
        if (strcmp(partition->device_name, emmc_raw_device) == 0)
            break;
    }

    free(emmc_raw_device);
    return get_size_in_gb(partition->size);
}

/*
 * Get the physical start offset of a partition.
 */
static loff_t get_partition_offset(const char *partition_name) {
    loff_t offset = 0;
    char path[PATH_MAX + 1];

    /* These days, the partition name is fancy symlink to the device node. */
    size_t len = readlink(partition_name, path, PATH_MAX);
    if (len > 0)
        path[len] = '\0';
    else {
        fprintf(stderr, "readlink failure, using v->blk_device %s\n", partition_name);
        strcpy(path, partition_name);
    }

    /* Use the basename of the /dev/block node to construct the sysfs path.  Tis a bit messy. */
    char *name = basename(path);                        /* mmcblkXpYY */
    char sysfs[PATH_MAX + 1];                           /* have to be careful not to touch path */
    strcpy(sysfs, "/sys/block/");
    len = strlen(sysfs);
    memcpy(sysfs + len, name, 7);                       /* /sys/block/mmcblkX???... */
    len += 7;
    sysfs[len++] = '/';
    sysfs[len] = '\0';                                  /* /sys/block/mmcblkX/ */
    strncat(sysfs, name, PATH_MAX - strlen(sysfs));     /* /sys/block/mmcblkX/mmcblkXpYY */
    strncat(sysfs, "/start", PATH_MAX - strlen(sysfs)); /* /sys/block/mmcblkX/mmcblkXpYY/start */

    int fd = -1;
    fd = open(sysfs, O_RDONLY);
    if (fd < 0)
        goto out;

    memset(path, 0, sizeof(path));                      /* safe to reuse path, now */
    if (read(fd, path, PATH_MAX) <= 0)
        goto out;

    offset = strtoll(path, NULL, 0) * 512;
out:
    if (fd >= 0)
        close(fd);

    return offset;
}

int blkdiscard_partition(const char *partition_name,bool validate_erase) {
    int deviceFd, ret = -1;
    loff_t length, curpos = 0, endpos, erase_size;
    unsigned char * buffer=NULL;
    unsigned char * alignment_buffer = NULL;
    ssize_t result = 0;
    char *read_buffer=NULL;
    char *zero_buffer = NULL;
    time_t val_start_time = time(NULL);
    time_t val_end_time = time(NULL);

    deviceFd = open(partition_name, O_RDWR | O_SYNC);

    if (deviceFd < 0) {
        fprintf(stderr,"Failed to open partition%s\n",partition_name);
        goto Exit_Label;
    }

    /* Compute the size of partition */
    length = lseek64(deviceFd, 0, SEEK_END);
    if (length <= 0) {
        fprintf(stderr, "Error! size of partition is not proper\n");
        goto Exit_Label;
    }

    loff_t offset = get_partition_offset(partition_name);
    printf("Formatting partition %s of length %llx starting at %llx\n", partition_name, length, offset);

    offset = ERASE_SIZE_MIN - (offset % ERASE_SIZE_MIN);

    if (offset > 0 && offset < length) {
        printf("Aligning offset to %x boundary by moving %x bytes\n", ERASE_SIZE_MIN, offset);

        alignment_buffer = (unsigned char*)calloc(1, sizeof(char) * offset);

        if (alignment_buffer == NULL) {
             fprintf(stderr, "calloc failure in blkdiscard_partition for %d bytes\n", sizeof(char) * offset);
             goto Exit_Label;
        }

        lseek64(deviceFd, curpos, SEEK_SET);

        result = write_to_file(deviceFd, alignment_buffer, sizeof(char) * offset);

        if (result < 0) {
            fprintf(stderr, "failure while aligning writes to %d chunk\n", ERASE_SIZE_MIN);
            goto Exit_Label;
        } else {
            curpos += sizeof(char) * offset;
        }
    }

    endpos = length;

    while (curpos < endpos) {
        erase_size = endpos - curpos;

        if (erase_size > ERASE_SIZE_MAX) { erase_size = ERASE_SIZE_MAX; }

        if(erase_size >= ERASE_SIZE_MIN) {
            uint64_t range[2] = {curpos, erase_size};
            result = ioctl(deviceFd, BLKDISCARD, &range);
        } else {
            buffer = (unsigned char*)calloc(1, sizeof(char) * erase_size);

            if (buffer == NULL) {
                fprintf(stderr, "\n Allocation failed");
                goto Exit_Label;
            }

            lseek64( deviceFd, curpos, SEEK_SET);

            result = write_to_file(deviceFd, buffer, sizeof(char) * erase_size);

            if (result < 0) {
                fprintf(stderr, "Failure while writing zeros towards the end\n");
                goto Exit_Label;
            }

            free(buffer);
            buffer = NULL;
        }

        if (result >= 0) {
            curpos += erase_size;
        } else {
            fprintf(stderr,"Failed to wipe %s partition.  ioctl failure. errno %d\n", partition_name, errno);
            ret = result;
            goto Exit_Label;
        }
    }
    // Motorola , SVCOTA-3274, grxv63, 30-May-2014
    printf("Format complete for partition \n");

    sync();

    if (validate_erase) {
        printf("Validation of Erase for partition \n");
        time(&val_start_time);
        printf("Validation started at:%s \n",ctime(&val_start_time));
        result = 0;
        curpos=0;
        loff_t read_size=0;
        zero_buffer = (unsigned char*)calloc(1, sizeof(char) * READ_SIZE);

        if (zero_buffer == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            goto Exit_Label;
        }

        read_buffer = (unsigned char*)malloc(sizeof(char) * READ_SIZE);

        if (read_buffer == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            goto Exit_Label;
        }

        while (curpos < endpos) {
            read_size = endpos - curpos;

            if (read_size >= READ_SIZE) {
                read_size = READ_SIZE;
            }
            //Gets over ridden with the contents read from partition.
            memset(read_buffer,1,read_size);

            if(lseek64(deviceFd,curpos,SEEK_SET) < 0) {
               fprintf(stderr,"Failed to seek from current position %lld with error :%s \n",curpos,strerror(errno));
               goto Exit_Label;
            }

            result = read_from_file(deviceFd, read_buffer, sizeof(char) * read_size);

            if (result < 0) {
                fprintf(stderr, "Failed to read from partition \n");
                goto Exit_Label;
            }

            if (memcmp(read_buffer,zero_buffer,read_size) != 0) {
                fprintf(stderr,"Validation of %s partiiton failed,not all bytes read are zero\n",partition_name);
                fprintf(stderr,"Failed at current position %lld : \n",curpos);
                goto Exit_Label;
            }

               curpos += read_size;
        }
        free(read_buffer);
        read_buffer = NULL;
        printf("Validation passed for partition %s\n", partition_name);
        time(&val_end_time);
        printf("Validation end time : %s\n",ctime(&val_end_time));
        printf("Validation took %f minutes\n", difftime(val_end_time,val_start_time)/60);
    }
    // ENd SVCOTA-3274
    ret = 0;

Exit_Label:
    if (alignment_buffer != NULL) {
        free(alignment_buffer);
        alignment_buffer = NULL;
    }

    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
    if (read_buffer!= NULL) free(read_buffer);
    if (zero_buffer!= NULL) free(zero_buffer);

    sync();

    if (deviceFd > 0)
        close(deviceFd);

    return ret;
}

int print_super_block_info(char *partition_name, int *mount_count, char *last_mount_path, int *last_mount_time)
{
     struct ext3_super_block {
     __le32 s_inodes_count;
     __le32 s_blocks_count;
     __le32 s_r_blocks_count;
     __le32 s_free_blocks_count;
     __le32 s_free_inodes_count;
     __le32 s_first_data_block;
     __le32 s_log_block_size;
     __le32 s_log_frag_size;
     __le32 s_blocks_per_group;
     __le32 s_frags_per_group;
     __le32 s_inodes_per_group;
     __le32 s_mtime;
     __le32 s_wtime;
     __le16 s_mnt_count;
     __le16 s_max_mnt_count;
     __le16 s_magic;
     __le16 s_state;
     __le16 s_errors;
     __le16 s_minor_rev_level;
     __le32 s_lastcheck;
     __le32 s_checkinterval;
     __le32 s_creator_os;
     __le32 s_rev_level;
     __le16 s_def_resuid;
     __le16 s_def_resgid;

     __le32 s_first_ino;
     __le16 s_inode_size;
     __le16 s_block_group_nr;
     __le32 s_feature_compat;
     __le32 s_feature_incompat;
     __le32 s_feature_ro_compat;
     __u8 s_uuid[16];
     char s_volume_name[16];
     char s_last_mounted[64];
     __le32 s_algorithm_usage_bitmap;

     __u8 s_prealloc_blocks;
     __u8 s_prealloc_dir_blocks;
     __u16 s_reserved_gdt_blocks;

     __u8 s_journal_uuid[16];
     __le32 s_journal_inum;
     __le32 s_journal_dev;
     __le32 s_last_orphan;
     __le32 s_hash_seed[4];
     __u8 s_def_hash_version;
     __u8 s_reserved_char_pad;
     __u16 s_reserved_word_pad;
     __le32 s_default_mount_opts;
     __le32 s_first_meta_bg;
     __u32 s_reserved[190];
    };

    int fd;
    char boot[1024];
    struct ext3_super_block super_block;
    int block_size;
    int fragment_size;
    time_t mtime = time(NULL);
    time_t wtime = time(NULL);
    time_t lctime = time(NULL);

    fd = open(partition_name, O_RDONLY);

    /* Reads the boot section and the superblock */
    read(fd, boot, 1024);
    read(fd, &super_block, sizeof(struct ext3_super_block));

    printf("\n<<<<< Super block analysis for partition %s: >>>>>\n", partition_name);
    printf("inodes count              = %d\n", super_block.s_inodes_count);
    printf("blocks count              = %d\n", super_block.s_blocks_count);
    printf("reserved blocks count     = %d\n", super_block.s_r_blocks_count);
    printf("free blocks count         = %d\n", super_block.s_free_blocks_count);
    printf("free inodes count         = %d\n", super_block.s_free_inodes_count);
    printf("first data block number   = %d\n", super_block.s_first_data_block);
    block_size = 1024 * power(2, super_block.s_log_block_size);
    printf("block size                = %d\n", block_size);
    fragment_size = 1024 * power(2, super_block.s_log_frag_size);
    printf("fragment size             = %d\n", fragment_size);
    printf("blocks per group          = %d\n", super_block.s_blocks_per_group);
    printf("fragments per group       = %d\n", super_block.s_frags_per_group);
    printf("inodes per group          = %d\n", super_block.s_inodes_per_group);
    printf("number of inode groups    = %d\n", (super_block.s_inodes_count/super_block.s_inodes_per_group));
    printf("number of block groups    = %d\n", (super_block.s_blocks_count/super_block.s_blocks_per_group));

    mtime = (time_t ) super_block.s_mtime;
    *last_mount_time = (time_t) super_block.s_mtime;
    printf("time of last mount        = %d -> %s", super_block.s_mtime, ctime(&mtime));
    wtime = (time_t)super_block.s_wtime;
    printf("time of last write        = %d -> %s", super_block.s_wtime, ctime(&wtime));
    *mount_count =  super_block.s_mnt_count;
    printf("mount count               = %d\n", super_block.s_mnt_count);
    printf("maximal mount count       = %d\n", super_block.s_max_mnt_count);

    printf("magic signature           = 0x%x\n", super_block.s_magic);
    printf("file system state         = %d\n", super_block.s_state);
    printf("behaviour when detecting errors  = %d\n", super_block.s_errors);
    printf("minor revision level      = %d\n", super_block.s_minor_rev_level);

    lctime = (time_t)super_block.s_lastcheck;
    printf("time of last check        = %d -> %s", super_block.s_lastcheck, ctime(&lctime));
    printf("max. time between checks  = %d\n", super_block.s_checkinterval);
    printf("OS filesystem created     = %d\n", super_block.s_creator_os);

    printf("revision level            = %d\n", super_block.s_rev_level);
    printf("default uid for reserved blocks = %d\n", super_block.s_def_resuid);
    printf("default gid for reserved blocks = %d\n", super_block.s_def_resgid);
    printf("first non-reserved inode number = %d\n", super_block.s_first_ino);
    printf("size of on-disck inode structure = %d\n", super_block.s_inode_size);
    printf("block group number of this superblock = %d\n", super_block.s_block_group_nr);
    printf("compatible feature set = %d (has_journal ext_attr resize_ino)\n", decimal_binary(super_block.s_feature_compat));
    printf("incompatible feature set = %d (filetype)\n", decimal_binary(super_block.s_feature_incompat));
    printf("readonly-compatible feature set = %d (sparse_super_large_file)\n", decimal_binary(super_block.s_feature_ro_compat));

    printf("128-bit uuid for volume     = %x%x%x%x-%x%x-%x%x-%x%x-%x%x%x%x%x%x\n", super_block.s_uuid[0], super_block.s_uuid[1], super_block.s_uuid[2], super_block.s_uuid[3], super_block.s_uuid[4], super_block.s_uuid[5], super_block.s_uuid[6], super_block.s_uuid[7], super_block.s_uuid[8], super_block.s_uuid[9], super_block.s_uuid[10], super_block.s_uuid[11], super_block.s_uuid[12], super_block.s_uuid[13], super_block.s_uuid[14], super_block.s_uuid[15]);
    printf("volume name                 = %s\n", super_block.s_volume_name);
    memcpy(last_mount_path, super_block.s_last_mounted, 64);
    printf("Path of last mount point    = %s\n", super_block.s_last_mounted);
    printf("compression Algorithm       = %d\n", super_block.s_algorithm_usage_bitmap);
    printf("number of blocks to preallocate = %d\n", super_block.s_prealloc_blocks);
    printf("number to blocks to preallocate for dirs  = %d\n", super_block.s_prealloc_dir_blocks);

    printf("Journal UUID                = %x%x%x%x-%x%x-%x%x-%x%x-%x%x%x%x%x%x\n", super_block.s_journal_uuid[0], super_block.s_journal_uuid[1], super_block.s_journal_uuid[2], super_block.s_journal_uuid[3], super_block.s_journal_uuid[4], super_block.s_journal_uuid[5], super_block.s_journal_uuid[6], super_block.s_journal_uuid[7], super_block.s_journal_uuid[8], super_block.s_journal_uuid[9], super_block.s_journal_uuid[10], super_block.s_journal_uuid[11], super_block.s_journal_uuid[12], super_block.s_journal_uuid[13], super_block.s_journal_uuid[14], super_block.s_journal_uuid[15]);
    printf("Journal inode number        =  %d\n", super_block.s_journal_inum);
    printf("Journal device number       =  %d\n", super_block.s_journal_dev);
    printf("Journal last orphan         = %d\n", super_block.s_last_orphan);
    printf("hash seed                   = %d %d %d %d\n", super_block.s_hash_seed[0], super_block.s_hash_seed[1], super_block.s_hash_seed[2], super_block.s_hash_seed[3]);
    printf("Default hash version to use = %d\n", super_block.s_def_hash_version);
    printf("default mount options       = %d\n", super_block.s_default_mount_opts);
    printf("First metablock group id    = %d\n", super_block.s_first_meta_bg);
    printf("================================================================\n");

    close(fd);
    return 0; //success
}

int decimal_binary(int n)  /* Function to convert decimal to binary.*/
{
    int rem, i=1, binary=0;
    while (n!=0)
    {
        rem=n%2;
        n/=2;
        binary+=rem*i;
        i*=10;
    }
    return binary;
}

int power(int base, int exp)
{
    int res = 1;

    for (; exp; --exp)
        res *= base;

    return res;
}
