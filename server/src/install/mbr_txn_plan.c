/* mbr_txn_plan.c — pure MBR transaction policy; deliberately no device I/O. */
#include "mbr_txn_plan.h"
#include <string.h>

static int terminal(MjtState s) {
    return s == MJT_COMMITTED || s == MJT_ABORTED || s == MJT_RESTORED;
}

MjtResult mjt_init(MjtPlan *p, const MjtPolicy *policy) {
    if (!p || !policy || policy->format_root > 1 || policy->format_home > 1 ||
        policy->initialize_swap > 1 || policy->mount_home > 1 ||
        !policy->format_root)
        return MJT_INVALID;
    memset(p, 0, sizeof *p);
    p->policy = *policy;
    p->state = MJT_EMPTY;
    return MJT_OK;
}

static MjtResult reject(MjtResult r) { return r; }

MjtResult mjt_apply(MjtPlan *p, MjtEvent ev) {
    if (!p || ev < MJT_EV_PREFLIGHT_OK || ev > MJT_EV_RESTORE_VERIFIED)
        return MJT_INVALID;
    if (terminal(p->state)) return MJT_TERMINAL;
    if (p->sequence == UINT64_MAX) return MJT_INVALID;

    switch (ev) {
    case MJT_EV_PREFLIGHT_OK:
        if (p->state != MJT_EMPTY) return reject(MJT_ORDER);
        p->state = MJT_PREFLIGHTED;
        break;
    case MJT_EV_BACKUP_DURABLE:
        if (p->state != MJT_PREFLIGHTED) return reject(MJT_ORDER);
        p->state = MJT_BACKUP_DURABLE;
        break;
    case MJT_EV_JOURNAL_DURABLE:
        if (p->state != MJT_BACKUP_DURABLE) return reject(MJT_ORDER);
        p->state = MJT_JOURNAL_DURABLE;
        break;
    case MJT_EV_ROOT_FORMAT_BEGIN:
        if (p->state != MJT_JOURNAL_DURABLE || p->root_format_started)
            return reject(MJT_ORDER);
        p->root_format_started = 1;
        p->state = MJT_FORMATTING;
        break;
    case MJT_EV_ROOT_FORMAT_DONE:
        if (p->state != MJT_FORMATTING || !p->root_format_started || p->root_format_done)
            return reject(MJT_ORDER);
        p->root_format_done = 1;
        break;
    case MJT_EV_HOME_FORMAT_BEGIN:
        if (p->state != MJT_FORMATTING || !p->root_format_done ||
            !p->policy.format_home || p->home_format_started)
            return reject(p->policy.format_home ? MJT_ORDER : MJT_CONSENT);
        p->home_format_started = 1;
        break;
    case MJT_EV_HOME_FORMAT_DONE:
        if (p->state != MJT_FORMATTING || !p->home_format_started || p->home_format_done)
            return reject(MJT_ORDER);
        p->home_format_done = 1;
        break;
    case MJT_EV_SWAP_INIT_BEGIN:
        if (p->state != MJT_FORMATTING || !p->root_format_done ||
            (p->policy.format_home && !p->home_format_done) ||
            !p->policy.initialize_swap || p->swap_started)
            return reject(p->policy.initialize_swap ? MJT_ORDER : MJT_CONSENT);
        p->swap_started = 1;
        break;
    case MJT_EV_SWAP_INIT_DONE:
        if (p->state != MJT_FORMATTING || !p->swap_started || p->swap_done)
            return reject(MJT_ORDER);
        p->swap_done = 1;
        break;
    case MJT_EV_FILESYSTEMS_READY:
        if (p->state != MJT_FORMATTING || !p->root_format_done ||
            (p->policy.format_home && !p->home_format_done) ||
            (p->policy.initialize_swap && !p->swap_done))
            return reject(MJT_ORDER);
        p->state = MJT_FILESYSTEMS_READY;
        break;
    case MJT_EV_ROOT_MOUNTED:
        if (p->state != MJT_FILESYSTEMS_READY || (p->mount_mask & MJT_MOUNT_ROOT))
            return reject(MJT_ORDER);
        p->mount_mask |= MJT_MOUNT_ROOT;
        p->state = MJT_MOUNTS_ACTIVE;
        break;
    case MJT_EV_HOME_MOUNTED:
        if (p->state != MJT_MOUNTS_ACTIVE || !(p->mount_mask & MJT_MOUNT_ROOT) ||
            !p->policy.mount_home || (p->mount_mask & MJT_MOUNT_HOME))
            return reject(p->policy.mount_home ? MJT_ORDER : MJT_CONSENT);
        p->mount_mask |= MJT_MOUNT_HOME;
        break;
    case MJT_EV_HOME_UNMOUNTED:
        if (p->state != MJT_MOUNTS_ACTIVE || !(p->mount_mask & MJT_MOUNT_HOME))
            return reject(MJT_ORDER);
        p->mount_mask &= ~MJT_MOUNT_HOME;
        break;
    case MJT_EV_ROOT_UNMOUNTED:
        if (p->state != MJT_MOUNTS_ACTIVE || !(p->mount_mask & MJT_MOUNT_ROOT) ||
            (p->mount_mask & MJT_MOUNT_HOME))
            return reject(MJT_ORDER);
        p->mount_mask &= ~MJT_MOUNT_ROOT;
        p->state = MJT_FILESYSTEMS_READY;
        break;
    case MJT_EV_GRUB_WRITE_BEGIN:
        if (p->state != MJT_MOUNTS_ACTIVE || !(p->mount_mask & MJT_MOUNT_ROOT) ||
            (p->policy.mount_home && !(p->mount_mask & MJT_MOUNT_HOME)))
            return reject(MJT_ORDER);
        p->grub_write_started = 1;
        p->state = MJT_GRUB_WRITE_STARTED;
        break;
    case MJT_EV_GRUB_WRITE_VERIFIED:
        if (p->state != MJT_GRUB_WRITE_STARTED || !p->grub_write_started ||
            p->grub_write_verified)
            return reject(MJT_ORDER);
        p->grub_write_verified = 1;
        p->state = MJT_GRUB_VERIFIED;
        break;
    case MJT_EV_COMMIT:
        if (p->state != MJT_GRUB_VERIFIED || !p->grub_write_verified ||
            p->mount_mask != 0)
            return reject(MJT_ORDER);
        p->state = MJT_COMMITTED;
        break;
    case MJT_EV_FAILURE:
        if (p->state < MJT_JOURNAL_DURABLE) {
            p->state = MJT_ABORTED;
        } else if (p->grub_write_started) {
            p->state = MJT_RECOVERY_REQUIRED;
        } else {
            p->state = MJT_ABORTED;
        }
        p->failure_seen = 1;
        break;
    case MJT_EV_RESTORE_VERIFIED:
        if (p->state != MJT_RECOVERY_REQUIRED || !p->grub_write_started)
            return reject(MJT_ORDER);
        p->state = MJT_RESTORED;
        p->mount_mask = 0;
        break;
    default:
        return MJT_INVALID;
    }
    p->sequence++;
    return MJT_OK;
}

MjtRecoveryAction mjt_recovery_action(const MjtPlan *p) {
    if (!p || p->state == MJT_EMPTY || p->state == MJT_PREFLIGHTED ||
        p->state == MJT_BACKUP_DURABLE || p->state == MJT_COMMITTED ||
        p->state == MJT_ABORTED || p->state == MJT_RESTORED)
        return MJT_RECOVER_NONE;
    if (p->grub_write_started) return MJT_RECOVER_RESTORE_GRUB;
    if (p->root_format_started || p->home_format_started || p->swap_started)
        return MJT_RECOVER_INSPECT_FILESYSTEMS;
    if (p->mount_mask) return MJT_RECOVER_CLEANUP_MOUNTS;
    return MJT_RECOVER_NONE;
}
