/* mbr_install_plan.h — one-pass, read-only orchestration for a future MBR install.
 * This module probes exactly once and composes selection, mount and GRUB plans.
 * It never formats, mounts, edits fstab, writes sectors or authorizes writes. */
#ifndef LORD_MBR_INSTALL_PLAN_H
#define LORD_MBR_INSTALL_PLAN_H

#include "mbr_preflight.h"
#include "mbr_mount_plan.h"
#include "mbr_grub_guard.h"

typedef enum {
    MIP_OK = 0,
    MIP_BAD_INPUT = -1,
    MIP_PROBE_FAILED = -2,
    MIP_STALE = -3,
    MIP_SELECTION = -4,
    MIP_MOUNT_PLAN = -5,
    MIP_GRUB_GUARD = -6
} MipResult;

typedef struct {
    DiskSnapshot disk;
    MpPlan mounts;
    MggGuardPlan grub;
    int writes_authorized; /* invariant: always zero */
} MipPlan;

/* fd must already be open read-only. Fingerprint comparison, MBR parsing and
 * snapshot conversion share one probe result (no second probe/TOCTOU window).
 * busy is a fresh fail-closed fact from the caller. grub_core_bytes must be
 * measured for the exact shipped GRUB image. Preserved /home requires its
 * detected filesystem and UUID. A successful result is still dry-run only. */
MipResult mbr_install_plan_build_fd(int fd, uint64_t disk_sectors,
                                    unsigned logical_sector_size,
                                    const char *expected_fingerprint,
                                    int bios_booted, int disk_busy,
                                    const MmPlan *selection,
                                    const char *const fstype[DS_MAX_PARTITIONS + 1],
                                    const char *preserved_home_uuid,
                                    uint64_t grub_core_bytes,
                                    int replacement_confirmed,
                                    MipPlan *out);
#endif
