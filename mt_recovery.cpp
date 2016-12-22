/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fs_mgr.h>
#include <bootloader_message/bootloader_message.h>
#include "cutils/properties.h"
#include "bootloader.h"
#include "roots.h"
#include "device.h"
#include "install.h"
#include "adb_install.h"
#include "mt_recovery.h"

/* (MUST SYNC) Android default defines from recovery.cpp */
#define LAST_LOG_FILE "/cache/recovery/last_log"

/* (MUST SYNC) Android default variables from recovery.cpp */
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *LOCALE_FILE = "/cache/recovery/last_locale";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *MT_UPDATE_STAGE_FILE = "/cache/recovery/last_mtupdate_stage";
extern char* stage;
extern RecoveryUI* ui;

/* (MUST SYNC) Android default functions from recovery.cpp */
extern int get_menu_selection(const char* const * headers, const char* const * items,
                   int menu_only, int initial_selection, Device* device);
extern void copy_log_file(const char* source, const char* destination, bool append);
extern void finish_recovery(const char *send_intent);
extern void wipe_data(int confirm, Device* device);
extern int erase_volume(const char *volume);
extern void copy_logs();
extern char* browse_directory(const char* path, Device* device);
extern void choose_recovery_file(Device* device);
extern void run_graphics_test(Device* device);

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16
    const char *restore_data_path = NULL;
#endif //SUPPORT_DATA_BACKUP_RESTORE

#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD)
const char *SDCARD2_ROOT = "/sdcard2";
#endif

#if defined(CACHE_MERGE_SUPPORT)
const char *DATA_CACHE_ROOT = "/data/.cache";
#endif


const char *MOTA_RESULT_FILE = "/data/data/com.mediatek.systemupdate/files/updateResult";

bool check_otaupdate_done(void)
{
    struct bootloader_message boot;
    std::string err;
    if (!read_bootloader_message(&boot, &err)) {
        LOGE("%s\n", err.c_str());
        // If fails, leave a zeroed bootloader_message.
        memset(&boot, 0, sizeof(boot));
    }

    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    const char *arg = strtok(boot.recovery, "\n");

    if (arg != NULL && !strcmp(arg, "sdota")) {
        LOGI("Got arguments from boot message is %s\n", boot.recovery);
        return true;
    } else {
        LOGI("no boot messages %s\n", boot.recovery);
        return false;
    }
}

void update_modem_property(void)
{
#ifdef EXTERNAL_MODEM_UPDATE
    char modem_prop[PROPERTY_VALUE_MAX];
    char value[PROPERTY_VALUE_MAX];
    int prop_read =0;

    printf("Reset persist.sys.extmddlprogress...\n");

    if (ensure_path_mounted("/data") == 0) {
        memset(value, 0, sizeof(value));
        property_get("persist.sys.extmddlprogress", modem_prop, "");

        if (modem_prop != NULL) {
            prop_read = atoi(modem_prop);
            printf("persist.sys.extmddlprogress=%d\n", prop_read);
            property_set("persist.sys.extmddlprogress", value);
        }

        ensure_path_unmounted("/data");
    } else {
        printf("in [update_modem_property] /data mount failed\n");
    }
#endif
}

#ifdef __cplusplus
extern "C" {
#endif
bool is_support_nvdata(void) {
    Volume* v = volume_for_path("/nvdata");
    if (v == NULL) {
        LOGI("nvdata not supported!\n");
        return false;
    } else {
        LOGI("nvdata is supported!\n");
        return true;
    }
}

void write_all_log(void)
{
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    sync();  // For good measure.
}

#ifdef __cplusplus
}
#endif

static int mt_compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09

int sdcard_restore_directory(const char *path, Device *device) {

    const char *MENU_HEADERS[] = { "Choose a backup file to restore:",
                                   path,
                                   "",
                                   NULL };
    DIR *d;
    struct dirent *de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        ensure_path_unmounted(SDCARD_ROOT);
        return 0;
    }

    int d_size = 0;
    int d_alloc = 10;
    char **dirs = (char**)malloc(d_alloc * sizeof(char *));
    int z_size = 1;
    int z_alloc = 10;
    char **zips = (char**)malloc(z_alloc * sizeof(char *));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') {
                continue;
            }
            if (name_len == 2 && de->d_name[0] == '.' && de->d_name[1] == '.') {
                continue;
            }

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 7 &&
                   strncasecmp(de->d_name + (name_len - 7), ".backup", 7) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char *), mt_compare_string);
    qsort(zips, z_size, sizeof(char *), mt_compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char *));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char *));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(nullptr, zips, 1, chosen_item, device);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is sdcard_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = sdcard_restore_directory(new_path, device);
            if (result >= 0) {
                break;
            }
        } else {
            // selected a backup file:  attempt to restore it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui->Print("\n-- Restore %s ...\n", item);
            //do restore and update result
            mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--restore_data=%s\n", new_path);
            result = userdata_restore(new_path, 0);
            mt_clear_bootloader_message();
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) {
        free(zips[i]);
    }
    free(zips);

    if (result != -1) {
        ensure_path_unmounted(SDCARD_ROOT);
    }
    return result;
}

#endif //SUPPORT_DATA_BACKUP_RESTORE

void prompt_error_message(int reason)
{
    switch (reason) {
        case INSTALL_NO_SDCARD:
            ui->Print("No SD-Card.\n");
            break;
        case INSTALL_NO_UPDATE_PACKAGE:
            ui->Print("Can not find update.zip.\n");
            break;
        case INSTALL_NO_KEY:
            ui->Print("Failed to load keys\n");
            break;
        case INSTALL_SIGNATURE_ERROR:
            ui->Print("Signature verification failed\n");
            break;
        case INSTALL_CORRUPT:
            ui->Print("The update.zip is corrupted\n");
            break;
        case INSTALL_FILE_SYSTEM_ERROR:
            ui->Print("Can't create/copy file\n");
            break;
        case INSTALL_ERROR:
            ui->Print("Update.zip is not correct\n");
            break;
    }
}

bool remove_mota_file(const char *file_name)
{
    int ret = 0;

    //LOG_INFO("[%s] %s\n", __func__, file_name);

    ret = unlink(file_name);

    if (ret == 0) {
        return true;
    }

    if (ret < 0 && errno == ENOENT) {
        return true;
    }

    return false;
}

void write_result_file(const char *file_name, int result)
{
  if (get_nand_type() == 1) {  /* EMMC */

    if (INSTALL_SUCCESS == result) {
        set_ota_result(1);
    } else {
        set_ota_result(0);
    }

  }
  else {  /* NAND */
    char dir_name[256];
    DIR *dirp = NULL;

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    if (p == NULL) {
      fprintf(stdout, "dir_mane = '%s' has no '/' \n", dir_name);
      return;
    } else {
      *p = 0;
    }
    fprintf(stdout, "dir_name = %s\n", dir_name);

    dirp = opendir(dir_name);
    if (dirp == NULL) {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0666)) {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            return;
        }
    }

    if ((closedir(dirp)) == -1) {
      fprintf(stdout, "can not close dir '%s' : %s\n", dir_name, strerror(errno));
      return;
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0644);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        return;
    }

    //LOG_INFO("[%s] %s %d\n", __func__, file_name, result);

    char buf[4];
    if (INSTALL_SUCCESS == result) {
        strcpy(buf, "1");
    } else {
        strcpy(buf, "0");
    }
    write(result_fd, buf, 1);
    close(result_fd);
  }
}

void mt_reset_mtupdate_stage(void)
{
    if ((unlink(MT_UPDATE_STAGE_FILE) < 0) && (errno != ENOENT))
        printf("unlink %s failed: %s\n", MT_UPDATE_STAGE_FILE, strerror(errno));
}

static int mt_prompt_force_upgrade(Device* device)
{
    const char* items[] = { " No",
                            " No",
                            " No",
                            " No",
                            " No",
                            " No",
                            " No",
                            " Yes -- delete all user data",   // [7]
                            " No",
                            " No",
                            " No",
                            NULL };

    int chosen_item = get_menu_selection(nullptr, items, 1, 0, device);
    if (chosen_item != 7) {
        return -1;
    }
    return 0;
}

/* Check execution status, and wipe cache/data if necessary
   This function is used by mt_prompt_and_wait_install(), and mt_prompt_and_wait();

   Parameters
   status: status code from prompt_and_wait()
   wipe_cache: 0=erase /cache, 1=erase /data

   Return Values
   Always 0
*/
int mt_prompt_and_wait_check_status(int status, bool wipe_cache)
{
    if (status == INSTALL_SUCCESS && wipe_cache) {
        if (wipe_cache == 1) {
            ui->Print("\n-- Wiping cache (at package request)...\n");
            mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_cache\n");
        } else {
            ui->Print("\n-- Wiping data (at package request)...\n");
            mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_data\n");
        }
        if (wipe_cache == 1) {
            if (erase_volume("/cache")) {
                ui->Print("Cache wipe failed.\n");
            } else {
                ui->Print("Cache wipe complete.\n");
            }
        }
        mt_clear_bootloader_message();
    }
    return status;
}

/* [Install] Handling install errors after install_package()
   This function is used by mt_prompt_and_wait_install()

   Parameters
   status: status code from prompt_and_wait()
   from:   Install package string to be shown on UI
   ret:    return value for prompt_and_wait()
   prompt: prompt user on error

   Return Values
   0: Caller mt_prompt_and_wait() shall go on.
   1: Caller mt_prompt_and_wait() shall return Device::NO_ACTION in
*/
static int mt_prompt_and_wait_check_status_install(int &status, const char *from,
    Device::BuiltinAction &ret, int prompt)
{
    if (status >= 0) {
        if (status != INSTALL_SUCCESS) {
            ui->SetBackground(RecoveryUI::ERROR);
            if (prompt)
                prompt_error_message(status);
            ui->Print("Installation aborted.\n");
        } else if (!ui->IsTextVisible()) {
            finish_recovery(NULL);
            ret = Device::NO_ACTION;
            return 1;
        } else {
            update_modem_property();
            ui->Print("\nInstall from %s complete.\n", from);
            finish_recovery(NULL);
        }
    }
    return 0;
}

/* [Install] Install packages form given deivce
   This function is used by mt_prompt_and_wait()

   Parameters
   device: UI device from prompt_and_wait()
   status: status code from prompt_and_wait()
   dir_tmp: path to temporarily directory, ex: /cache
   dir_browse: path to install package's directory, ex: /sdcard, /sdcard2
   from:   Install package string to be shown on UI
   ret:    return value for prompt_and_wait()
   prompt: prompt user on error

   Return Values
   0: Caller mt_prompt_and_wait() shall go on.
   1: Caller mt_prompt_and_wait() shall return Device::NO_ACTION in
*/
static int mt_prompt_and_wait_install(Device* device, int &status,
    const char *dir_tmp, const char *dir_browse, bool &wipe_cache,
    const char *from, Device::BuiltinAction &ret, int prompt)
{
    if (dir_tmp)
        ensure_path_mounted(dir_tmp);

    if(ensure_path_mounted(dir_browse) != 0)  {
      if(!strcasecmp(dir_browse, "sdcard"))
         ui->Print("\n Mount sdcard fail ,please change other sdcard or insert sdcard\n");
    }

    char* path = browse_directory(dir_browse, device);
    if (path == NULL) {
        ui->Print("\n-- No package file selected.\n");
        return 0;
    }

    ui->Print("\n-- Install %s ...\n", path);
    mt_set_sdcard_update_bootloader_message();  /* use MTK enhanced api instead. */

    status = install_package(path, &wipe_cache,
            TEMPORARY_INSTALL_FILE, true, 0/*retry_count*/);
#ifdef SUPPORT_DATA_BACKUP_RESTORE
    if (status == INSTALL_PROMPT_FORCE_UPGRADE) {
        if (mt_prompt_force_upgrade(device) == 0) {
            set_force_upgrade();
            status = install_package(path, &wipe_cache, TEMPORARY_INSTALL_FILE, true, 0/*retry_count*/);
        }
    }
#endif
    mt_reset_mtupdate_stage();
    ensure_path_unmounted(dir_browse);
    mt_prompt_and_wait_check_status(status, wipe_cache);
    return mt_prompt_and_wait_check_status_install(status, "sdcard", ret, 0);
}

Device::BuiltinAction
mt_prompt_and_wait(Device* device, int status) {
    Device::BuiltinAction ret = Device::NO_ACTION;
    bool wipe_cache;

    for (;;) {
        ui->Print("\n");
        if (!check_otaupdate_done()) {
            LOGI("[1]check the otaupdate is done!\n");
            finish_recovery(NULL);
            switch (status) {
                case INSTALL_SUCCESS:
                case INSTALL_NONE:
                    ui->SetBackground(RecoveryUI::NO_COMMAND);
                    break;

                case INSTALL_ERROR:
                case INSTALL_CORRUPT:
                    ui->SetBackground(RecoveryUI::ERROR);
                    break;
            }
            ui->SetProgressType(RecoveryUI::EMPTY);

            int chosen_item = get_menu_selection(nullptr, device->GetMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            Device::BuiltinAction chosen_action = device->InvokeMenuItem(chosen_item);
            // clear screen
            ui->Print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");

            switch (chosen_action) {
                case Device::NO_ACTION:
                    break;

                case Device::REBOOT:
                case Device::SHUTDOWN:
                case Device::REBOOT_BOOTLOADER:
                    return chosen_action;

                case Device::WIPE_DATA:
                    wipe_data(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return Device::NO_ACTION;
                    break;

                case Device::WIPE_CACHE:
                    {
                        ui->Print("\n-- Wiping cache...\n");
                        mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_cache\n");
                        erase_volume("/cache");
                        ui->Print("Cache wipe complete.\n");
                        mt_clear_bootloader_message();
                        if (!ui->IsTextVisible()) return Device::NO_ACTION;
                    }
                    break;

                case Device::APPLY_SDCARD:
                    modified_flash = true;
                    if (mt_prompt_and_wait_install(device, status, CACHE_ROOT, SDCARD_ROOT, wipe_cache, "sdcard", ret, 0))
                        return ret;
                  break;
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
                case Device::APPLY_SDCARD2:
                    if (mt_prompt_and_wait_install(device, status, CACHE_ROOT, SDCARD2_ROOT, wipe_cache, "sdcard2", ret, 0))
                        return ret;
                    break;
#endif
                case Device::VIEW_RECOVERY_LOGS:
                  choose_recovery_file(device);
                  break;

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09
                case Device::USER_DATA_BACKUP:
                    {
                        modified_flash = true;
                        ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
                        ui->SetProgressType(RecoveryUI::EMPTY);
                        ui->ShowProgress(1.0, 0);

                        status = userdata_backup(0);    // 0 means backup whole partition

                        if (status == INSTALL_SUCCESS) {
                            ui->SetBackground(RecoveryUI::NONE);
                            ui->SetProgressType(RecoveryUI::EMPTY);
                            ui->Print("Backup user data complete.\n");
                            finish_recovery(NULL);
                        } else {
                            ui->SetBackground(RecoveryUI::ERROR);
                        }
                    }
                    break;
                case Device::USER_DATA_RESTORE:
                    {
                        modified_flash = true;
                        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
                            ui->SetBackground(RecoveryUI::ERROR);
                            ui->Print("No SD-Card.\n");
                            break;
                        }

                        status = sdcard_restore_directory(SDCARD_ROOT, device);

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::ERROR);
                            } else {
                                mt_clear_bootloader_message();
                                ui->SetBackground(RecoveryUI::NONE);
                                ui->SetProgressType(RecoveryUI::EMPTY);
                                if (!ui->IsTextVisible()) {
                                    finish_recovery(NULL);
                                    return Device::NO_ACTION;  // reboot if logs aren't visible
                                } else {
                                    ui->Print("Restore user data complete.\n");
                                    finish_recovery(NULL);
                                }
                            }
                        }
                    }
                    break;
#endif

#ifdef ROOT_CHECK
                case Device::CHECK_ROOT:
                    {
                        int check_result = 1;
                        modified_flash = true;
                        check_result = root_check();
                        if (check_result==0) {
                            ui->Print("\nSystem check SUCCESS!!\n ");
                        } else {
                            ui->Print("\nSystem check FAIL!!\n ");
                        }
                    }
                    break;
#endif
                case Device::APPLY_ADB_SIDELOAD:
                    {
                        modified_flash = true;
                        ensure_path_mounted(CACHE_ROOT);
                        status = apply_from_adb(ui, &wipe_cache, TEMPORARY_INSTALL_FILE);

                        mt_prompt_and_wait_check_status(status, wipe_cache);

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::ERROR);
                                ui->Print("Installation aborted.\n");
                                copy_logs();
                            } else if (!ui->IsTextVisible()) {
                                return Device::NO_ACTION;  // reboot if logs aren't visible
                            } else {
                                update_modem_property();
                                ui->Print("\nInstall from ADB complete.\n");
                            }
                        }
                    }
                    break;
                case Device::MOUNT_SYSTEM:
                    if (ensure_path_mounted("/system") != -1) {
                        ui->Print("Mounted /system.\n");
                    }
                    break;
                case Device::RUN_GRAPHICS_TEST:
                    run_graphics_test(device);
                    break;
                default:
                    break;
            }
        } else {
            ui->SetProgressType(RecoveryUI::EMPTY);
            ui->Print("\n Please continue to update your system !\n");
            int chosen_item = get_menu_selection(nullptr, device->GetForceMenuItems(), 0, 0, device);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            Device::BuiltinAction chosen_action = device->InvokeForceMenuItem(chosen_item);

            switch (chosen_action)
            {
                case Device::REBOOT:
                case Device::SHUTDOWN:
                case Device::REBOOT_BOOTLOADER:
                    return chosen_action;

                case Device::APPLY_SDCARD:
                    modified_flash = true;
                    if (mt_prompt_and_wait_install(device, status, NULL, SDCARD_ROOT, wipe_cache, "sdcard", ret, 1))
                        return ret;
                    break;

#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
                case Device::APPLY_SDCARD2:
                    modified_flash = true;
                    if (mt_prompt_and_wait_install(device status, NULL, SDCARD2_ROOT, wipe_cache, "sdcard2", ret, 1))
                        return ret;
                    break;
#endif //SUPPORT_SDCARD2
                default:
                    break;
            }
        }
    } // for
}


void mt_set_sdcard_update_bootloader_message(void) {
    mt_set_bootloader_message("boot-recovery", NULL, stage, "sdota\n");
}

/* MTK turnkey wipe data function for smartphones */
static bool mt_wipe_data_sp(int confirm, Device* device) {
    struct phone_encrypt_state ps;
    if (confirm) {
        const char* items[] = { " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " No",
                                " Yes -- delete all user data",   // [7]
                                " No",
                                " No",
                                " No",
                                NULL };
        int chosen_item = get_menu_selection(nullptr, items, 1, 0, device);
        if (chosen_item != 7) {
            return false;
        }
    }

    ui->Print("\n-- Wiping data...\n");
    modified_flash = true;

    mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_data\n");
    device->PreWipeData();
    erase_volume("/data");
    if(is_support_nvdata()) {
        erase_volume("/nvdata");
    }
    erase_volume("/cache");
    device->PostWipeData();
    mt_clear_bootloader_message();
    ps.state = 0;
    set_phone_encrypt_state(&ps);
    sync();

    ui->Print("Data wipe complete.\n");
    return true;
}

/* MTK turnkey wipe data function for wearable devices */
static bool mt_wipe_data_wearable(int confirm, Device* device) {
    struct phone_encrypt_state ps;
    if (confirm) {
        const char* items[] = { "No",
                                "No",
                                "No",
                                "No",
                                "Yes--delete all user data",   // [4]
                                "No",
                                "No",
                                NULL };

        int chosen_item = get_menu_selection(nullptr, items, 1, 0, device);
        if (chosen_item != 4) {
            return false;
        }
    }

    ui->Print("\n-- Wiping data...\n");
    mt_set_bootloader_message("boot-recovery", NULL, stage, "--wipe_data\n");
    device->PreWipeData();
    erase_volume("/data");
    if(is_support_nvdata()) {
        erase_volume("/nvdata");
    }
    erase_volume("/cache");
    device->PostWipeData();
    mt_clear_bootloader_message();
    ps.state = 0;
    set_phone_encrypt_state(&ps);
    sync();
    ui->Print("Data wipe complete.\n");
    return true;
}


/* MTK turnkey wipe data function */
bool mt_wipe_data(int confirm, Device* device)
{
  #ifdef MTK_WEARABLE_PLATFORM
  return mt_wipe_data_wearable(confirm, device);
  #else
  return mt_wipe_data_sp(confirm, device);
  #endif
}

int mt_main_arg(int arg)
{
  switch (arg)
    {
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16
          case 'd': restore_data_path = optarg; break;
#endif //SUPPORT_DATA_BACKUP_RESTORE
          default: break;
    }
    return 0;
}

int mt_main_init_sdcard2(void)
{
#if defined(SUPPORT_SDCARD2) && !defined(MTK_SHARED_SDCARD) //wschen 2012-11-15
      struct stat sd2_st;
      if (stat(SDCARD2_ROOT, &sd2_st) == -1) {
          if (errno == ENOENT) {
              if (mkdir(SDCARD2_ROOT, S_IRWXU | S_IRWXG | S_IRWXO) == 0) {
                  printf("Trun on SUPPORT_SDCARD2\n");
              } else {
                  printf("mkdir fail %s\n", strerror(errno));
              }
          }
      }
#endif
  return 0;
}

int mt_main_init_cache_merge(void)
{
#if defined(CACHE_MERGE_SUPPORT)
      // create symlink from CACHE_ROOT to DATA_CACHE_ROOT
      if (ensure_path_mounted(DATA_CACHE_ROOT) == 0) {
          if (mkdir(DATA_CACHE_ROOT, 0770) != 0) {
              if (errno != EEXIST) {
                  LOGE("Can't mkdir %s (%s)\n", DATA_CACHE_ROOT, strerror(errno));
                  return 1;
              }
          }
          rmdir(CACHE_ROOT); // created in init.rc
          if (symlink(DATA_CACHE_ROOT, CACHE_ROOT)) {
              if (errno != EEXIST) {
                  LOGE("create symlink from %s to %s failed(%s)\n",
                                  DATA_CACHE_ROOT, CACHE_ROOT, strerror(errno));
                  return 1;
              }
          }
      } else {
          LOGE("mount %s error\n", DATA_CACHE_ROOT);
      }
#endif
  return 0;
}


int mt_main_wipe_data(int &status, Device* device, int wipe_cache)
{
#if 1 //wschen 2012-04-12
          struct phone_encrypt_state ps;
#endif
#if 1 //wschen 2012-03-21
          mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_data\n");
#endif
          if (!device->PreWipeData()) status = INSTALL_ERROR;
          if (erase_volume("/data")) status = INSTALL_ERROR;
          if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
          if (is_support_nvdata()) {
              if (erase_volume("/nvdata")) status = INSTALL_ERROR;
          }
          if (!device->PostWipeData()) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
          if (status != INSTALL_SUCCESS) {
              ui->Print("Data wipe failed.\n");
          } else {
              mt_clear_bootloader_message();
#if 1 //wschen 2012-04-12
              ps.state = 0;
              set_phone_encrypt_state(&ps);
#endif
              sync();
          }
#endif
    return status;
}


int mt_main_wipe_cache(int &status)
{
    mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_cache\n");
    if (erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
    if (status != INSTALL_SUCCESS) {
        ui->Print("Cache wipe failed.\n");
    } else {
        mt_clear_bootloader_message();
    }
#endif
    return status;
}

const char *mt_main_backup_restore(int &status)
{
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16
  modified_flash = true;
  if (restore_data_path) {
      if (ensure_path_mounted(SDCARD_ROOT) != 0) {
          ui->SetBackground(RecoveryUI::ERROR);
          ui->Print("No SD-Card.\n");
          status = INSTALL_ERROR;
      } else {
          status = userdata_restore((char *)restore_data_path, 0);
          mt_clear_bootloader_message();
          ensure_path_unmounted(SDCARD_ROOT);
      }
  }
  return restore_data_path;
#else
  return NULL;
#endif /* SUPPORT_DATA_BACKUP_RESTORE */

}

int mt_main_update_package(int &status, const char *update_package, bool *wipe_cache)
{
      mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--update_package=%s\n", update_package);
      status = install_package(update_package, wipe_cache, TEMPORARY_INSTALL_FILE, true, 0/*retry_count*/);
      if (status == INSTALL_SUCCESS && *wipe_cache) {
          mt_set_bootloader_message("boot-recovery", NULL, stage, "recovery\n--wipe_cache\n");
          if (erase_volume("/cache")) {
              LOGE("Cache wipe (request by package) failed.");
          }
          mt_clear_bootloader_message();
      }

      if (status == INSTALL_SUCCESS) {
          update_modem_property();
      }

  prompt_error_message(status);
  return status;
}

int mt_main_write_result(int &status, const char *update_package)
{
      if (update_package) {
          if ((status == INSTALL_SUCCESS) && update_package) {
              fprintf(stdout, "write result : remove_mota_file\n");
              remove_mota_file(update_package);
          }
          write_result_file(MOTA_RESULT_FILE, status);
      }
  return 0;
}

