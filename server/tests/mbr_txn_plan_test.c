#include "../src/install/mbr_txn_plan.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); failures++; } } while (0)

static MjtPolicy policy_all(void) {
    MjtPolicy p = {1, 1, 1, 1};
    return p;
}
static void advance_to_journal(MjtPlan *p) {
    CHECK(mjt_apply(p, MJT_EV_PREFLIGHT_OK) == MJT_OK);
    CHECK(mjt_apply(p, MJT_EV_BACKUP_DURABLE) == MJT_OK);
    CHECK(mjt_apply(p, MJT_EV_JOURNAL_DURABLE) == MJT_OK);
}
static void test_full_commit(void) {
    MjtPlan p; MjtPolicy q = policy_all();
    CHECK(mjt_init(&p, &q) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_JOURNAL_DURABLE) == MJT_ORDER);
    CHECK(p.sequence == 0 && p.state == MJT_EMPTY);
    advance_to_journal(&p);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_NONE);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_INSPECT_FILESYSTEMS);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_SWAP_INIT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_SWAP_INIT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FILESYSTEMS_READY) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_MOUNTED) == MJT_ORDER);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_MOUNTED) == MJT_OK);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_CLEANUP_MOUNTS);
    CHECK(mjt_apply(&p, MJT_EV_GRUB_WRITE_BEGIN) == MJT_OK);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_CLEANUP_MOUNTS);
    CHECK(mjt_apply(&p, MJT_EV_GRUB_WRITE_VERIFIED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_COMMIT) == MJT_ORDER); /* mounted target cannot be committed */
    CHECK(mjt_apply(&p, MJT_EV_HOME_UNMOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_UNMOUNTED) == MJT_OK);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_RESTORE_GRUB);
    CHECK(mjt_apply(&p, MJT_EV_COMMIT) == MJT_OK);
    CHECK(p.state == MJT_COMMITTED && mjt_recovery_action(&p) == MJT_RECOVER_NONE);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_TERMINAL);
}
static void test_consent_and_preserve_home(void) {
    MjtPlan p; MjtPolicy q = {1, 0, 0, 1};
    CHECK(mjt_init(&p, &q) == MJT_OK);
    advance_to_journal(&p);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_FORMAT_BEGIN) == MJT_CONSENT);
    CHECK(mjt_apply(&p, MJT_EV_SWAP_INIT_BEGIN) == MJT_CONSENT);
    CHECK(mjt_apply(&p, MJT_EV_FILESYSTEMS_READY) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_GRUB_WRITE_BEGIN) == MJT_OK);
}
static void test_failure_boundaries(void) {
    MjtPlan p; MjtPolicy q = policy_all();
    CHECK(mjt_init(&p, &q) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_OK);
    CHECK(p.state == MJT_ABORTED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_NONE);

    CHECK(mjt_init(&p, &q) == MJT_OK);
    advance_to_journal(&p);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_OK);
    CHECK(p.state == MJT_ABORTED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_NONE);

    CHECK(mjt_init(&p, &q) == MJT_OK);
    advance_to_journal(&p);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_OK);
    CHECK(p.state == MJT_ABORTED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_INSPECT_FILESYSTEMS);

    CHECK(mjt_init(&p, &q) == MJT_OK);
    advance_to_journal(&p);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_SWAP_INIT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_SWAP_INIT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FILESYSTEMS_READY) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_GRUB_WRITE_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_OK);
    CHECK(p.state == MJT_RECOVERY_REQUIRED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_CLEANUP_MOUNTS);
    CHECK(mjt_apply(&p, MJT_EV_RESTORE_VERIFIED) == MJT_ORDER); /* mounts must be gone first */
    CHECK(mjt_apply(&p, MJT_EV_HOME_UNMOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_UNMOUNTED) == MJT_OK);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_RESTORE_GRUB);
    CHECK(mjt_apply(&p, MJT_EV_RESTORE_VERIFIED) == MJT_OK);
    CHECK(p.state == MJT_RESTORED && p.mount_mask == 0);
}
static void test_invalid_policy_and_mount_order(void) {
    MjtPlan p; MjtPolicy q = {0,0,0,0};
    CHECK(mjt_init(&p, &q) == MJT_INVALID);
    q = (MjtPolicy){1,1,0,0};
    CHECK(mjt_init(&p, &q) == MJT_INVALID); /* cannot format /home and then leave it unmounted */
    q = (MjtPolicy){1,0,0,0};
    CHECK(mjt_init(&p, &q) == MJT_OK);
    advance_to_journal(&p);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_BEGIN) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_FORMAT_DONE) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FILESYSTEMS_READY) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_HOME_MOUNTED) == MJT_CONSENT);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_UNMOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_GRUB_WRITE_BEGIN) == MJT_ORDER);

    /* A failure with an active mount must enter recovery, not terminal abort. */
    CHECK(mjt_apply(&p, MJT_EV_ROOT_MOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_FAILURE) == MJT_OK);
    CHECK(p.state == MJT_RECOVERY_REQUIRED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_CLEANUP_MOUNTS);
    CHECK(mjt_apply(&p, MJT_EV_ROOT_UNMOUNTED) == MJT_OK);
    CHECK(mjt_apply(&p, MJT_EV_CLEANUP_VERIFIED) == MJT_OK);
    CHECK(p.state == MJT_ABORTED);
    CHECK(mjt_recovery_action(&p) == MJT_RECOVER_INSPECT_FILESYSTEMS);
}
int main(void) {
    test_full_commit();
    test_consent_and_preserve_home();
    test_failure_boundaries();
    test_invalid_policy_and_mount_order();
    if (failures) { fprintf(stderr,"%d transaction planner test(s) failed\n",failures); return 1; }
    puts("MBR transaction planner tests passed");
    return 0;
}
