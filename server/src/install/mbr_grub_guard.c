/* mbr_grub_guard.c — planning-only BIOS GRUB preflight; never authorizes writes. */
#include "mbr_grub_guard.h"
#include "mbr_disk.h"

#include <string.h>

MggGuardResult mbr_grub_guard_plan(const DiskSnapshot *disk,
                                   uint64_t core_image_bytes,
                                   int bios_booted, int disk_busy,
                                   const char *expected_fingerprint,
                                   const char *observed_fingerprint,
                                   int replacement_confirmed,
                                   MggGuardPlan *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!disk || !core_image_bytes || !expected_fingerprint ||
        !observed_fingerprint || !out) return MGG_GUARD_BAD_INPUT;
    if (!bios_booted) return MGG_GUARD_NEED_BIOS;
    if (disk_busy) return MGG_GUARD_BUSY;
    if (!mbr_fingerprint_matches(expected_fingerprint, observed_fingerprint))
        return MGG_GUARD_STALE;
    if (!replacement_confirmed) return MGG_GUARD_CONFIRM_REPLACEMENT;

    uint64_t available = 0, required = 0;
    if (mbr_grub_gap_check(disk, core_image_bytes, &available, &required) != MGG_OK)
        return MGG_GUARD_GAP;

    out->backup_first_lba = 0;
    out->backup_sector_count = available + 1; /* MBR + entire candidate embedding gap */
    out->embedding_first_lba = 1;
    out->embedding_sector_count = required;
    out->required_core_sectors = required;
    out->existing_boot_manager_may_be_replaced = 1;
    out->writes_authorized = 0;
    return MGG_GUARD_OK;
}
