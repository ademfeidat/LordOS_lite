/* mbr_preflight.c — read-only MBR preflight; deliberately no installer write hooks. */
#include "mbr_preflight.h"

#include <string.h>

const char *mbr_manual_preflight_fd(int fd, uint64_t disk_sectors,
                                    unsigned logical_sector_size,
                                    const char *expected_tag,
                                    int bios_booted, int disk_busy,
                                    const MmPlan *plan,
                                    const char *const fstype[DS_MAX_PARTITIONS + 1],
                                    DiskSnapshot *fresh_snapshot) {
    if (fd < 0 || !expected_tag || !plan || !fstype || !fresh_snapshot)
        return "install.manual.mbr.badPlan";

    MbrTable table;
    char observed[40];
    if (mbr_probe_fd(fd, disk_sectors, logical_sector_size,
                     &table, observed, sizeof observed) != MBR_OK)
        return "install.manual.mbr.badTable";
    if (!mbr_fingerprint_matches(expected_tag, observed))
        return "install.manual.mbr.diskChanged";
    if (ds_from_mbr(&table, disk_sectors, logical_sector_size, fresh_snapshot))
        return "install.manual.mbr.badTable";

    MmFacts facts;
    memset(&facts, 0, sizeof facts);
    facts.bios_booted = !!bios_booted;
    facts.disk_busy = !!disk_busy;
    facts.disk = fresh_snapshot;
    for (unsigned i = 0; i <= DS_MAX_PARTITIONS; i++) {
        const char *fs = fstype[i] ? fstype[i] : "";
        size_t n = 0;
        while (n < sizeof facts.fstype[i] && fs[n]) n++;
        if (n >= sizeof facts.fstype[i]) return "install.manual.mbr.badPlan";
        memcpy(facts.fstype[i], fs, n + 1);
    }
    return mm_decide(&facts, plan);
}
