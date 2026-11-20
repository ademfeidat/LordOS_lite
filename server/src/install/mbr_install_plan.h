/* mbr_install_plan.h — one-pass, read-only orchestration for a future MBR install.
 * This module probes exactly once and composes selection, mount and GRUB plans.
 * It never formats, mounts, edits fstab, writes sectors or authorizes writes. */
#ifndef LORD_MBR_INSTALL_PLAN_H
#define LORD_MBR_INSTALL_PLAN_H

#include "mbr_mount_plan.h"
#include "mbr_grub_guard.h"
#include "mbr_device_identity.h"

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
 * detected filesystem and canonical UUID. Returns NULL on success, otherwise
 * a localized installer error key. A successful result is still dry-run only. */
const char *mbr_install_plan_build_fd(int fd, uint64_t disk_sectors,
                                      unsigned logical_sector_size,
                                      const char *expected_fingerprint,
                                      int bios_booted,
                                      const MmPlan *selection,
                                      const char *const fstype[DS_MAX_PARTITIONS + 1],
                                      const char *preserved_home_uuid,
                                      uint64_t grub_core_bytes,
                                      int replacement_confirmed,
                                      MipPlan *out);

/* Production-facing wrapper: proves the open fd is the selected whole-disk
 * read-only block device and derives geometry from BLK ioctls before planning.
 * The regular-file dry-run API above remains available only for tests. */
const char *mbr_install_plan_build_block_fd(
                                      int fd, const MbrDeviceIdentity *expected,
                                      const char *expected_fingerprint,
                                      int bios_booted, int disk_busy,
                                      const MmPlan *selection,
                                      const char *const fstype[DS_MAX_PARTITIONS + 1],
                                      const char *preserved_home_uuid,
                                      uint64_t grub_core_bytes,
                                      int replacement_confirmed,
                                      MipPlan *out);
#endif
