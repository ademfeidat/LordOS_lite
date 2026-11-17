/* mbr_manual.h — read-only validation for manual installs onto existing DOS/MBR partitions.
 * No partition-table edits or device I/O. New MBR partitions are intentionally unsupported here. */
#ifndef LORD_MBR_MANUAL_H
#define LORD_MBR_MANUAL_H

#include "disk_snapshot.h"

#define MM_ROOT_MIN_BYTES (8ull << 30)
#define MM_HOME_MIN_BYTES (1ull << 30)
#define MM_SWAP_MIN_BYTES (256ull << 20)
#define MM_SWAP_MAX_BYTES (64ull << 30)

typedef struct {
    int bios_booted;
    int disk_busy;
    const DiskSnapshot *disk;
    char fstype[DS_MAX_PARTITIONS + 1][16];
} MmFacts;

typedef struct {
    unsigned root_part;       /* required existing MBR primary/logical partition number */
    unsigned home_part;       /* optional */
    unsigned swap_part;       /* optional */
    int root_format;          /* must be explicitly set: root is always formatted */
    int home_format;          /* preserve existing /home filesystem when false */
    unsigned swap_file_mib;   /* optional swapfile; mutually exclusive with swap_part */
} MmPlan;

/* NULL means valid. This validates selection only; it does not perform formatting or bootloader work. */
const char *mm_decide(const MmFacts *facts, const MmPlan *plan);
int mm_home_fs_ok(const char *fstype);

/* Convert parsed install-run fields without narrowing or treating omitted rootFormat as true. */
int mm_plan_from_fields(long root_part, long home_part, long swap_part,
                        int root_format, int root_format_seen, int home_format,
                        long swap_file_mib, MmPlan *out);

#endif
