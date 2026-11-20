/* mbr_install_plan.c — single-snapshot dry-run orchestration. */
#include "mbr_install_plan.h"
#include "mbr_disk.h"

#include <string.h>

MipResult mbr_install_plan_build_fd(int fd, uint64_t disk_sectors,
                                    unsigned logical_sector_size,
                                    const char *expected_fingerprint,
                                    int bios_booted, int disk_busy,
                                    const MmPlan *selection,
                                    const char *const fstype[DS_MAX_PARTITIONS + 1],
                                    const char *preserved_home_uuid,
                                    uint64_t grub_core_bytes,
                                    int replacement_confirmed,
                                    MipPlan *out) {
    if (out) memset(out, 0, sizeof *out);
    if (fd < 0 || !expected_fingerprint || !selection || !fstype || !out ||
        !grub_core_bytes) return MIP_BAD_INPUT;

    MbrTable table;
    char observed[40];
    if (mbr_probe_fd(fd, disk_sectors, logical_sector_size,
                    &table, observed, sizeof observed) != MBR_OK)
        return MIP_PROBE_FAILED;
    if (!mbr_fingerprint_matches(expected_fingerprint, observed))
        return MIP_STALE;

    MipPlan plan;
    memset(&plan, 0, sizeof plan);
    if (ds_from_mbr(&table, disk_sectors, logical_sector_size, &plan.disk))
        return MIP_PROBE_FAILED;

    MmFacts facts;
    memset(&facts, 0, sizeof facts);
    facts.bios_booted = !!bios_booted;
    facts.disk_busy = !!disk_busy;
    facts.disk = &plan.disk;
    for (unsigned i = 0; i <= DS_MAX_PARTITIONS; i++) {
        const char *fs = fstype[i] ? fstype[i] : "";
        size_t n = strlen(fs);
        if (n >= sizeof facts.fstype[i]) return MIP_BAD_INPUT;
        memcpy(facts.fstype[i], fs, n + 1);
    }
    if (mm_decide(&facts, selection)) return MIP_SELECTION;

    const char *home_fs = selection->home_part ?
        facts.fstype[selection->home_part] : NULL;
    if (mp_build(&plan.disk, selection, bios_booted, disk_busy,
                 home_fs, preserved_home_uuid, &plan.mounts))
        return MIP_MOUNT_PLAN;

    MggGuardResult gr = mbr_grub_guard_plan(
        &plan.disk, grub_core_bytes, bios_booted, disk_busy,
        expected_fingerprint, observed, replacement_confirmed, &plan.grub);
    if (gr != MGG_GUARD_OK) return MIP_GRUB_GUARD;

    /* This orchestration milestone is intentionally incapable of authorizing IO. */
    plan.writes_authorized = 0;
    plan.grub.writes_authorized = 0;
    *out = plan;
    return MIP_OK;
}
