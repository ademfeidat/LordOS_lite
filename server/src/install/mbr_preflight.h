/* mbr_preflight.h — read-only last-moment MBR snapshot and selection gate. */
#ifndef LORD_MBR_PREFLIGHT_H
#define LORD_MBR_PREFLIGHT_H

#include "mbr_disk.h"
#include "mbr_manual.h"

/* Re-probes an already-open read-only disk fd, checks the install-disks fingerprint,
 * converts the fresh table and applies server-side selection policy. Never writes.
 * Returns NULL only when this read-only preflight passes. */
const char *mbr_manual_preflight_fd(int fd, uint64_t disk_sectors,
                                    unsigned logical_sector_size,
                                    const char *expected_tag,
                                    int bios_booted, int disk_busy,
                                    const MmPlan *plan,
                                    const char *const fstype[DS_MAX_PARTITIONS + 1],
                                    DiskSnapshot *fresh_snapshot);

#endif
