/* mbr_grub_guard.c — planning-only BIOS GRUB preflight; never authorizes writes. */
#include "mbr_grub_guard.h"
#include "mbr_disk.h"

#include <string.h>

MggGuardResult mbr_grub_guard_plan(const DiskSnapshot *disk,
                                   uint64_t core_image_bytes,
                                   uint64_t write_set_bytes,
                                   int bios_booted, int disk_busy,
                                   const char *expected_fingerprint,
                                   const char *observed_fingerprint,
                                   int replacement_confirmed,
                                   MggGuardPlan *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!disk || !core_image_bytes || !write_set_bytes ||
        write_set_bytes > UINT64_MAX - 511 || !expected_fingerprint ||
        !observed_fingerprint || !out) return MGG_GUARD_BAD_INPUT;
    if (!bios_booted) return MGG_GUARD_NEED_BIOS;
    if (disk_busy) return MGG_GUARD_BUSY;
    if (!mbr_fingerprint_matches(expected_fingerprint, observed_fingerprint))
        return MGG_GUARD_STALE;
    if (!replacement_confirmed) return MGG_GUARD_CONFIRM_REPLACEMENT;

    uint64_t available = 0, required = 0;
    if (mbr_grub_gap_check(disk, core_image_bytes, &available, &required) != MGG_OK)
        return MGG_GUARD_GAP;
    uint64_t write_sectors=(write_set_bytes+511)/512;
    if (!write_sectors || write_sectors-1>available) return MGG_GUARD_GAP;

    out->backup_first_lba = 0;
    out->backup_sector_count = write_sectors;
    out->embedding_first_lba = 1;
    out->embedding_sector_count = required > write_sectors-1 ? required : write_sectors-1;
    out->required_core_sectors = required;
    out->write_set_sector_count = write_sectors;
    out->existing_boot_manager_may_be_replaced = 1;
    out->writes_authorized = 0;
    return MGG_GUARD_OK;
}
