/* mbr_install_plan.c — single-snapshot dry-run orchestration. */
#include "mbr_install_plan.h"
#include "mbr_disk.h"

#include <string.h>

const char *mbr_install_plan_build_fd(int fd, uint64_t disk_sectors,
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
        !grub_core_bytes) return "install.manual.mbr.badPlan";

    MbrTable table;
    char observed[40];
    if (mbr_probe_fd(fd, disk_sectors, logical_sector_size,
                    &table, observed, sizeof observed) != MBR_OK)
        return "install.manual.mbr.badTable";
    if (!mbr_fingerprint_matches(expected_fingerprint, observed))
        return "install.manual.mbr.diskChanged";

    MipPlan plan;
    memset(&plan, 0, sizeof plan);
    if (ds_from_mbr(&table, disk_sectors, logical_sector_size, &plan.disk))
        return "install.manual.mbr.badTable";

    MmFacts facts;
    memset(&facts, 0, sizeof facts);
    facts.bios_booted = !!bios_booted;
    facts.disk_busy = !!disk_busy;
    facts.disk = &plan.disk;
    for (unsigned i = 0; i <= DS_MAX_PARTITIONS; i++) {
        const char *fs = fstype[i] ? fstype[i] : "";
        size_t n = strlen(fs);
        if (n >= sizeof facts.fstype[i]) return "install.manual.mbr.badPlan";
        memcpy(facts.fstype[i], fs, n + 1);
    }
    const char *why = mm_decide(&facts, selection);
    if (why) return why;

    const char *home_fs = selection->home_part ?
        facts.fstype[selection->home_part] : NULL;
    if (mp_build(&plan.disk, selection, bios_booted, disk_busy,
                 home_fs, preserved_home_uuid, &plan.mounts))
        return "install.manual.mbr.mountPlan";

    MggGuardResult gr = mbr_grub_guard_plan(
        &plan.disk, grub_core_bytes, bios_booted, disk_busy,
        expected_fingerprint, observed, replacement_confirmed, &plan.grub);
    switch (gr) {
        case MGG_GUARD_OK: break;
        case MGG_GUARD_NEED_BIOS: return "install.manual.mbr.needBios";
        case MGG_GUARD_BUSY: return "install.manual.mbr.busy";
        case MGG_GUARD_STALE: return "install.manual.mbr.diskChanged";
        case MGG_GUARD_CONFIRM_REPLACEMENT: return "install.manual.mbr.confirmBootloader";
        case MGG_GUARD_GAP: return "install.manual.mbr.noGrubGap";
        default: return "install.manual.mbr.badPlan";
    }

    /* This orchestration milestone is intentionally incapable of authorizing IO. */
    plan.writes_authorized = 0;
    plan.grub.writes_authorized = 0;
    *out = plan;
    return NULL;
}

const char *mbr_install_plan_build_block_fd(
                                      int fd, const MbrDeviceIdentity *expected,
                                      const char *expected_fingerprint,
                                      int bios_booted, int disk_busy,
                                      const MmPlan *selection,
                                      const char *const fstype[DS_MAX_PARTITIONS + 1],
                                      const char *preserved_home_uuid,
                                      uint64_t grub_core_bytes,
                                      int replacement_confirmed,
                                      MipPlan *out) {
    MbrDeviceIdentity observed;
    MdiResult identity=mbr_device_identity_check_fd(fd,expected,&observed);
    if(identity!=MDI_OK) return "install.manual.mbr.deviceIdentity";
    return mbr_install_plan_build_fd(fd,observed.disk_sectors,
                                     observed.logical_sector_size,
                                     expected_fingerprint,bios_booted,disk_busy,
                                     selection,fstype,preserved_home_uuid,
                                     grub_core_bytes,replacement_confirmed,out);
}
