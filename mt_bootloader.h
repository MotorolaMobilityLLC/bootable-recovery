/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/
#ifndef MT_BOOTLOADER_H_
#define MT_BOOTLOADER_H_

#include <sys/types.h>
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int mt_set_bootloader_message(const char *command, const char *status, const char *stage,
    const char *fmt, ...);
int mt_clear_bootloader_message(void);
int mt_get_bootloader_message_block(struct bootloader_message *out,
                                        const Volume* v);
int mt_set_bootloader_message_block(const struct bootloader_message *in,
                                        const Volume* v);
int get_bootloader_message(struct bootloader_message *out);
int set_bootloader_message(const struct bootloader_message *in);

#define OTA_RESULT_OFFSET    (2560)

/* Read and write the phone encrypt state from the "misc" partition.
 */
int get_phone_encrypt_state(struct phone_encrypt_state *out);
int set_phone_encrypt_state(const struct phone_encrypt_state *in);
int set_ota_result(int result);
int get_nand_type(void);

#ifdef __cplusplus
}
#endif

#endif

