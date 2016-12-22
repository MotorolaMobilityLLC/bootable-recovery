/*
* Copyright (C) 2014 MediaTek Inc.
* Modification based on code covered by the mentioned copyright
* and/or permission notice(s).
*/

#include <string.h>
#include <errno.h>
#include <fs_mgr.h>
#include "common.h"
#include "ui.h"
#include "roots.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "install.h"
#include "mt_install.h"
#include "mt_pmt.h"
#include "bootloader.h"

extern RecoveryUI* ui;
int update_from_data;

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09
#include "mt_check_partition.h"
#endif

#ifdef EXTERNAL_MODEM_UPDATE
#define BROMLITE_NAME  "META-INF/com/google/android/bromLite-binary"
#define BROMLITE_PATH  "/tmp/bromLite-binary"
#define MODEM_FILE_PATH  "/system/etc/firmware/modem"
#define EXT_MD_IOC_MAGIC 'E'
#define EXT_MD_IOCTL_R8_DOWNLOAD _IO(EXT_MD_IOC_MAGIC, 106)
#define EXT_MD_MONITOR_DEV "/dev/ext_md_ctl0"

static int
try_update_modem(const char *path) {
    int i, fd, pipe_fd[2], status;
    pid_t pid;
    ZipEntry *temp_entry = NULL;
    FILE *from_child;
    ZipArchive zip;

    //unzip bromlite to /tmp
    MemMapping map;
    if (sysMapFile(path, &map) != 0) {
        LOGE("failed to map file %s\n", path);
        return INSTALL_CORRUPT;
    }

    status = mzOpenZipArchive(map.addr, map.length, &zip);
    if (status != 0) {
        LOGE("Can't open %s\n(%s)\n", path, status != -1 ? strerror(status) : "bad");
        sysReleaseMap(&map);
        return INSTALL_CORRUPT;
    }
    temp_entry = (ZipEntry *)mzFindZipEntry(&zip, BROMLITE_NAME);
    if (temp_entry == NULL) {
        LOGE("Can't find %s, maybe don't need to upgrade modem \n", BROMLITE_NAME);
        mzCloseZipArchive(&zip);
        sysReleaseMap(&map);
        return INSTALL_SUCCESS;
    }
    unlink(BROMLITE_PATH);
    fd = creat(BROMLITE_PATH, 0755);
    if (fd < 0) {
        mzCloseZipArchive(&zip);
        LOGE("Can't make %s\n", BROMLITE_PATH);
        sysReleaseMap(&map);
        return INSTALL_ERROR;
    }
    bool ok = mzExtractZipEntryToFile(&zip, temp_entry, fd);
    close(fd);
    mzCloseZipArchive(&zip);
    if (!ok) {
        LOGE("Can't copy %s\n", BROMLITE_NAME);
        sysReleaseMap(&map);
        return INSTALL_ERROR;
    }

    //update modem from MODEM_FILE_PATH
    ensure_path_mounted("/system");
    pid  = fork();
    if(pid == 0){
        execl(BROMLITE_PATH, "bromLite-binary", MODEM_FILE_PATH, 0);
        fprintf(stdout, "E:Can't run %s (%s)\n", BROMLITE_PATH, strerror(errno));
        _exit(-1);
    }
    sysReleaseMap(&map);
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
        return INSTALL_ERROR;
    }

    return INSTALL_SUCCESS;
}
#endif


int mt_really_install_package_external_modem(int &result, const char *path)
{
#ifdef EXTERNAL_MODEM_UPDATE
        ui->Print("Installing update Modem...\n");
        result = try_update_modem(path);
        if (result != INSTALL_SUCCESS) {
            LOGE("try_update_modem fail \n");
            return result;
        }
#endif
    return 0;
}

static int mt_try_update_binary_backup_restore(int &ret, const char *path)
{
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09
        //Skip userdata restore if updating from /data with no /data layout change
        if (!usrdata_changed && update_from_data) {
            ui->Print("/data offset remains the same no need to restore usrdata\n");
        } else {
            if (part_size_changed) {
                if (ensure_path_mounted("/sdcard") != 0) {
                    LOGE("Can't mount %s\n", path);
                    ret=INSTALL_NO_SDCARD;
                    return 1;
                }
                if (userdata_restore(backup_path, 1)) {
                    ret=INSTALL_FILE_SYSTEM_ERROR;
                    return 1;
                }
                update_pmt_nand();
            }
        }
#endif //SUPPORT_DATA_BACKUP_RESTORE
    return 0;
}

int mt_try_update_binary(int &ret, const char *path)
{
    if (mt_try_update_binary_backup_restore(ret, path))
        return 1;

    return 0;
}

/* Setup backup restore check_part_size() before installing the package
   return values
   0: operation successful.
   1: there's some error and caller really_install_package() shall return "ret".
*/
static int mt_really_install_package_backup_restore(int &ret, const char *path, bool needs_mount, ZipArchive *zip, MemMapping *map)
{
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09
        update_from_data = 0;

        if (path && (path[0] == '@')) {
            update_from_data = 2;
        } else {
            Volume* v = volume_for_path(path);
            if (v && strcmp(v->mount_point, "/data") == 0) {
                update_from_data = 1;
            }
        }

        int retval = check_part_size(zip, update_from_data, path, map, needs_mount);
        if (retval == ERROR_CANNOT_BACKUP) {
            mt_clear_bootloader_message();
            ret=INSTALL_PROMPT_FORCE_UPGRADE;
            return 1;
        } else if (retval != CHECK_OK) {
            mt_clear_bootloader_message();
            ret=INSTALL_ERROR;
            return 1;
        }
#endif //SUPPORT_DATA_BACKUP_RESTORE
    return 0;
}

/* Setup secure boot before installing the package
   return values
   0: operation successful.
   1: there's some error and caller really_install_package() shall return "ret".
*/
int mt_really_install_package_sboot(int &ret, ZipArchive *zip, MemMapping *map)
{
    int err;
    return 0;
}

/* Turnkey feature setups before installing the package
   return values
   0: operation successful.
   1: there's some error and caller really_install_package() shall return "ret".
*/
int mt_really_install_package(int &ret, const char *path, bool needs_mount, ZipArchive *zip, MemMapping *map)
{
    /* Handling Backup Restore */
    if (mt_really_install_package_backup_restore(ret, path, needs_mount, zip, map))
        return 1;
    /* Handling Secure Boot */
    if (mt_really_install_package_sboot(ret, zip, map))
        return 1;
    return 0;
}


