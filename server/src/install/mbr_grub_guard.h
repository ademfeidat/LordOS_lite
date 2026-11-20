/* mbr_grub_guard.h — non-authorizing BIOS GRUB backup/write preflight model. */
#ifndef LORD_MBR_GRUB_GUARD_H
#define LORD_MBR_GRUB_GUARD_H
#include "mbr_grub_gap.h"

typedef enum {
    MGG_GUARD_OK = 0,
    MGG_GUARD_BAD_INPUT = -10,
    MGG_GUARD_NEED_BIOS = -11,
    MGG_GUARD_BUSY = -12,
    MGG_GUARD_STALE = -13,
    MGG_GUARD_CONFIRM_REPLACEMENT = -14,
    MGG_GUARD_GAP = -15
} MggGuardResult;

typedef struct {
    uint64_t backup_first_lba;       /* always 0: includes the MBR */
    uint64_t backup_sector_count;    /* proposed full pre-partition region */
    uint64_t embedding_first_lba;    /* candidate starts at LBA 1 */
    uint64_t embedding_sector_count;
    uint64_t required_core_sectors;
    uint64_t write_set_sector_count; /* exact audited GRUB write interval, including LBA 0 */
    int existing_boot_manager_may_be_replaced;
    int writes_authorized;           /* always 0 in this planning-only milestone */
} MggGuardPlan;

/* Builds a proposed backup/embedding plan only. write_set_bytes must come from
 * the verified shipped GRUB provenance manifest. Backup covers exactly that
 * contiguous interval from LBA 0. It does not read or write sectors, invoke
 * GRUB, prove the gap is unused, or authorize installation. The confirmation
 * acknowledges that an existing boot manager may be replaced. Fingerprints are
 * stale-snapshot tokens, not cryptographic identities. */
MggGuardResult mbr_grub_guard_plan(const DiskSnapshot *disk,
                                   uint64_t core_image_bytes,
                                   uint64_t write_set_bytes,
                                   int bios_booted, int disk_busy,
                                   const char *expected_fingerprint,
                                   const char *observed_fingerprint,
                                   int replacement_confirmed,
                                   MggGuardPlan *out);
#endif
