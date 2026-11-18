/* mbr_grub_backup.h — verified backup/restore for the audited BIOS GRUB write-set.
 * This module is testable on regular-file disk images. It does not invoke GRUB and
 * is not connected to install-run; callers must perform fresh disk preflight first. */
#ifndef LORD_INSTALL_MBR_GRUB_BACKUP_H
#define LORD_INSTALL_MBR_GRUB_BACKUP_H

#include "disk_snapshot.h"
#include <stdint.h>

#define MGB_MAX_RANGES 4
#define MGB_BACKUP_HEADER_SIZE 512

typedef struct {
    uint64_t offset;
    uint64_t length;
} MgbRange;

typedef struct {
    unsigned logical_sector_size;
    uint64_t disk_sectors;
    unsigned range_count;
    MgbRange ranges[MGB_MAX_RANGES];
    char grub_version[32];
    char grub_install_sha256[65];
    char grub_bios_setup_sha256[65];
    char modules_sha256[65];
} MgbSpec;

typedef enum {
    MGB_OK = 0,
    MGB_BAD_INPUT = -1,
    MGB_IO = -2,
    MGB_EXISTS = -3,
    MGB_BAD_BACKUP = -4,
    MGB_WRONG_DISK = -5,
    MGB_WRONG_LAYOUT = -6,
    MGB_INTEGRITY = -7
} MgbResult;

/* dir_fd must refer to a trusted directory on the installed root filesystem.
 * Backup creation reads disk_fd only; it creates name with O_EXCL|O_NOFOLLOW,
 * mode 0600, fsyncs the file and directory, then verifies the backup by reading
 * it back. disk_fd may be a regular file only for tests; production callers must
 * independently require a block device and a fresh read-only MBR preflight. */
MgbResult mgb_backup_fd(int disk_fd, int dir_fd, const char *name,
                        const DiskSnapshot *snapshot, const MgbSpec *spec);

/* Verifies backup format, digest, device identity, geometry, snapshot and GRUB
 * provenance without changing the disk. */
MgbResult mgb_verify_backup_fd(int disk_fd, int dir_fd, const char *name,
                               const DiskSnapshot *snapshot, const MgbSpec *spec);

/* Restores every recorded range, fsyncs the disk, then verifies every byte by
 * reading it back. It preserves the backup on every error. It is intended only
 * for immediate rollback in the same installer process with the same open disk
 * identity; it is not yet a reboot-time recovery mechanism. */
MgbResult mgb_restore_fd(int disk_fd, int dir_fd, const char *name,
                         const DiskSnapshot *snapshot, const MgbSpec *spec);

#endif
