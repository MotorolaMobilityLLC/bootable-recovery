/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <fcntl.h>
#include <string.h>

#include "bootloader.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "uncrypt/include/mt_bootloader_message_writer.h"
#define LOG_TAG "mt_bootloader_message_writer"
#include <log/log.h>

// ------------------------------
// for misc partitions on MTD
// ------------------------------

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

int mt_write_bootloader_message_mtd(const bootloader_message* in, const char *block_dev_path)
{
#if (defined  MTK_SLC_BUFFER_SUPPORT) ||(defined MTK_MLC_NAND_SUPPORT)
    char *dev_path = NULL;
    char *dev_name = NULL;
    size_t write_size;
    mtd_scan_partitions();

    dev_path = strrchr(block_dev_path, '@');
    if(dev_path)
        dev_name = strdup(dev_path+1);
    else
        dev_name = strdup(block_dev_path);

    const MtdPartition* part = mtd_find_partition_by_name(dev_name);
    if (part == nullptr || mtd_partition_info(part, nullptr, nullptr, &write_size)) {
        ALOGE("failed to find \"%s\"\n", dev_name);
        return MTD_WRITE_FAIL;
    }

    MtdReadContext* read = mtd_read_partition(part);
    if (read == nullptr) {
        ALOGE("failed to open \"%s\": %s\n", dev_name, strerror(errno));
        return MTD_WRITE_FAIL;
    }

    ssize_t size = write_size * MISC_PAGES;
    char data[size];
    ssize_t r = mtd_read_data(read, data, size);
    if (r != size) ALOGE("failed to read \"%s\": %s\n", dev_name, strerror(errno));
    mtd_read_close(read);
    if (r != size) return MTD_WRITE_FAIL;

    memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

    MtdWriteContext* write = mtd_write_partition(part);
    if (write == nullptr) {
        ALOGE("failed to open \"%s\": %s\n", dev_name, strerror(errno));
        return MTD_WRITE_FAIL;
    }
    if (mtd_write_data(write, data, size) != size) {
        ALOGE("failed to write \"%s\": %s\n", dev_name, strerror(errno));
        mtd_write_close(write);
        return MTD_WRITE_FAIL;
    }
    if (mtd_write_close(write)) {
        ALOGE("failed to finish \"%s\": %s\n", dev_name, strerror(errno));
        return MTD_WRITE_FAIL;
    }

    ALOGE("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
    return MTD_WRITE_FINISH;
#else /* NOT MTD */
   return WRITE_CONTINUE;
#endif
}
