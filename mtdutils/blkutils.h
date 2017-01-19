
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

#ifndef BLKUTILS_H_
#define BLKUTILS_H_

#include <sys/types.h>  // for size_t, etc.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlkPartition BlkPartition;

int block_scan_partitions();
const BlkPartition *block_find_partition_by_name(const char *name);
void getDeviceName(const char *name, char *device_name, int device_length);
int block_format_fs(const BlkPartition *partition, int wipe, const char *fstype);
int formatRawPtn(const char *location);
bool doValidateErase();
uint8_t blkutils_device_size(void);
int blkdiscard_partition(const char *partition_name,bool validate_erase);
int write_to_file(int fd, unsigned char *data, size_t length);
int read_from_file(int fd, unsigned char *data, size_t length);
int print_super_block_info(char *partition_name, int *mount_count, char *last_mount_path, int *last_mount_time);

#ifdef __cplusplus
}
#endif

#endif  // BLKUTILS_H_
