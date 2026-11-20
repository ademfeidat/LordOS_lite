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

typedef enum {
    MGC_OK = 0,
    MGC_BAD_INPUT = -1,
    MGC_IO = -2,
    MGC_OCCUPIED = -3
} MgcResult;

/* Read-only content check for the exact contiguous GRUB write interval starting
 * at byte zero. Every byte after the MBR sector and through the rounded-up
 * audited write-set end must be zero; any existing content is treated as user
 * data and blocks installation. Requires an O_RDONLY descriptor. */
MgcResult mbr_grub_gap_contents_check_fd(int fd, uint64_t disk_sectors,
                                         uint64_t write_set_bytes,
                                         uint64_t *first_nonzero_lba);
#endif
