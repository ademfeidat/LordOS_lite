/* mbr_grub_gap.h — read-only geometry check for a possible BIOS GRUB embedding gap. */
#ifndef LORD_MBR_GRUB_GAP_H
#define LORD_MBR_GRUB_GAP_H
#include "disk_snapshot.h"

typedef enum {
    MGG_OK = 0,
    MGG_BAD_SNAPSHOT = -1,
    MGG_NO_PARTITION_GAP = -2,
    MGG_TOO_SMALL = -3,
    MGG_BAD_IMAGE_SIZE = -4
} MggResult;

/* Checks only partition-table geometry. The gap may contain another bootloader
 * or data: this function does not prove that sectors are unused and authorizes
 * no writes. required_bytes must be measured from the exact GRUB core image. */
MggResult mbr_grub_gap_check(const DiskSnapshot *disk, uint64_t required_bytes,
                             uint64_t *available_sectors,
                             uint64_t *required_sectors);
#endif
