/* mbr_txn_plan.h — pure, write-disabled MBR transaction state machine.
 * No disk, filesystem, mount, journal-file or GRUB I/O is performed here. */
#ifndef LORD_INSTALL_MBR_TXN_PLAN_H
#define LORD_INSTALL_MBR_TXN_PLAN_H

#include <stdint.h>

typedef enum {
    MJT_EMPTY = 0,
    MJT_PREFLIGHTED,
    MJT_BACKUP_DURABLE,
    MJT_JOURNAL_DURABLE,
    MJT_FORMATTING,
    MJT_FILESYSTEMS_READY,
    MJT_MOUNTS_ACTIVE,
    MJT_GRUB_WRITE_STARTED,
    MJT_GRUB_VERIFIED,
    MJT_COMMITTED,
    MJT_ABORTED,
    MJT_RECOVERY_REQUIRED,
    MJT_RESTORED
} MjtState;

typedef enum {
    MJT_EV_PREFLIGHT_OK = 1,
    MJT_EV_BACKUP_DURABLE,
    MJT_EV_JOURNAL_DURABLE,
    MJT_EV_ROOT_FORMAT_BEGIN,
    MJT_EV_ROOT_FORMAT_DONE,
    MJT_EV_HOME_FORMAT_BEGIN,
    MJT_EV_HOME_FORMAT_DONE,
    MJT_EV_SWAP_INIT_BEGIN,
    MJT_EV_SWAP_INIT_DONE,
    MJT_EV_FILESYSTEMS_READY,
    MJT_EV_ROOT_MOUNTED,
    MJT_EV_HOME_MOUNTED,
    MJT_EV_HOME_UNMOUNTED,
    MJT_EV_ROOT_UNMOUNTED,
    MJT_EV_GRUB_WRITE_BEGIN,
    MJT_EV_GRUB_WRITE_VERIFIED,
    MJT_EV_COMMIT,
    MJT_EV_FAILURE,
    MJT_EV_RESTORE_VERIFIED
} MjtEvent;

typedef enum {
    MJT_OK = 0,
    MJT_INVALID = -1,
    MJT_ORDER = -2,
    MJT_CONSENT = -3,
    MJT_TERMINAL = -4
} MjtResult;

typedef enum {
    MJT_RECOVER_NONE = 0,
    MJT_RECOVER_CLEANUP_MOUNTS,
    MJT_RECOVER_INSPECT_FILESYSTEMS,
    MJT_RECOVER_RESTORE_GRUB
} MjtRecoveryAction;

enum {
    MJT_MOUNT_ROOT = 1u << 0,
    MJT_MOUNT_HOME = 1u << 1
};

typedef struct {
    uint8_t format_root; /* must be explicitly true for this installation mode */
    uint8_t format_home; /* false means preserve the selected /home */
    uint8_t initialize_swap;
    uint8_t mount_home;
} MjtPolicy;

typedef struct {
    MjtState state;
    MjtPolicy policy;
    uint64_t sequence;
    uint32_t mount_mask;
    uint8_t root_format_started;
    uint8_t root_format_done;
    uint8_t home_format_started;
    uint8_t home_format_done;
    uint8_t swap_started;
    uint8_t swap_done;
    uint8_t grub_write_started;
    uint8_t grub_write_verified;
    uint8_t failure_seen;
} MjtPlan;

/* Initialize only a plan. This function performs no I/O. */
MjtResult mjt_init(MjtPlan *plan, const MjtPolicy *policy);

/* Apply one event. The sequence advances only for accepted transitions.
 * A caller must persist and fsync each accepted record before carrying out
 * the corresponding external operation; this module cannot enforce that. */
MjtResult mjt_apply(MjtPlan *plan, MjtEvent event);

/* Conservative boot-time classification of an interrupted transaction.
 * A journal that says GRUB writing began requires verified GRUB-range restore.
 * Any format-start marker requires filesystem inspection: mkfs is not reversible. */
MjtRecoveryAction mjt_recovery_action(const MjtPlan *plan);

#endif
