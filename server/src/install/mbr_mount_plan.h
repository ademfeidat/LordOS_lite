/* mbr_mount_plan.h — pure, dry-run filesystem/mount plan for existing MBR partitions.
 * This module never opens devices and never formats or mounts anything. */
#ifndef LORD_MBR_MOUNT_PLAN_H
#define LORD_MBR_MOUNT_PLAN_H

#include "mbr_manual.h"

#define MP_MAX_ACTIONS 6
#define MP_FSTAB_MAX 192

typedef enum {
    MP_FORMAT_ROOT_EXT4,
    MP_MOUNT_ROOT,
    MP_FORMAT_HOME_EXT4,
    MP_MOUNT_HOME,
    MP_MKSWAP_PARTITION,
    MP_CREATE_SWAPFILE
} MpActionKind;

typedef struct {
    MpActionKind kind;
    unsigned partition;       /* zero for swapfile */
    unsigned size_mib;        /* swapfile only */
    char target[16];          /* /, /home, or swapfile path */
    char filesystem[16];      /* ext4, preserved fs, swap, or none */
} MpAction;

typedef struct {
    unsigned action_count;
    MpAction actions[MP_MAX_ACTIONS];
    unsigned fstab_count;
    char fstab[4][MP_FSTAB_MAX];
} MpPlan;

/* Build an inspectable plan from a validated MBR snapshot and selection.
 * preserved_home_uuid is required only when preserving /home; it must be a
 * canonical 36-character UUID. Root and newly initialized swap use explicit
 * post-format UUID placeholders in fstab. Returns 0 on success, -1 otherwise. */
int mp_build(const DiskSnapshot *disk, const MmPlan *selection,
             int bios_booted, int disk_busy,
             const char *preserved_home_fstype,
             const char *preserved_home_uuid, MpPlan *out);

#endif
