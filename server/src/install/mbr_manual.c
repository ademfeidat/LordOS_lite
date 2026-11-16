/* mbr_manual.c — conservative manual partition selection for BIOS + DOS/MBR. */
#include "mbr_manual.h"

#include <string.h>

static const DsPartition *find_part(const DiskSnapshot *d, unsigned number) {
    for (unsigned i = 0; i < d->partition_count; i++)
        if (d->partitions[i].number == number) return &d->partitions[i];
    return NULL;
}

static int size_at_least(const DsPartition *p, uint64_t bytes) {
    uint64_t sectors = p->last_lba - p->first_lba + 1;
    return sectors <= UINT64_MAX / 512 && sectors * 512 >= bytes;
}

int mm_home_fs_ok(const char *s) {
    return s && (!strcmp(s, "ext4") || !strcmp(s, "ext3") ||
                 !strcmp(s, "ext2") || !strcmp(s, "xfs") ||
                 !strcmp(s, "btrfs"));
}

const char *mm_decide(const MmFacts *f, const MmPlan *p) {
    if (!f || !p || !f->disk) return "install.manual.mbr.badPlan";
    const DiskSnapshot *d = f->disk;
    if (d->kind != DS_TABLE_MBR || d->logical_sector_size != 512 ||
        d->total_sectors < 2) return "install.manual.mbr.badTable";
    if (!f->bios_booted) return "install.manual.mbr.needBios";
    if (f->disk_busy) return "install.manual.mbr.busy";
    if (!p->root_part || !p->root_format || p->root_part > DS_MAX_PARTITIONS ||
        p->home_part > DS_MAX_PARTITIONS || p->swap_part > DS_MAX_PARTITIONS ||
        p->root_part == p->home_part || p->root_part == p->swap_part ||
        (p->home_part && p->home_part == p->swap_part) ||
        (p->swap_part && p->swap_file_mib)) return "install.manual.mbr.badPlan";

    const DsPartition *root = find_part(d, p->root_part);
    if (!root) return "install.manual.mbr.noSuchPartition";
    if (root->is_esp || root->type_code != 0x83)
        return "install.manual.mbr.rootType";
    if (!size_at_least(root, MM_ROOT_MIN_BYTES))
        return "install.manual.mbr.rootTooSmall";

    if (p->home_part) {
        const DsPartition *home = find_part(d, p->home_part);
        if (!home) return "install.manual.mbr.noSuchPartition";
        if (home->is_esp || home->type_code != 0x83)
            return "install.manual.mbr.homeType";
        if (!size_at_least(home, MM_HOME_MIN_BYTES))
            return "install.manual.mbr.homeTooSmall";
        if (!p->home_format && !mm_home_fs_ok(f->fstype[p->home_part]))
            return "install.manual.mbr.homeUnknownFs";
    }

    if (p->swap_part) {
        const DsPartition *swap = find_part(d, p->swap_part);
        if (!swap) return "install.manual.mbr.noSuchPartition";
        if (swap->type_code != 0x82)
            return "install.manual.mbr.swapType";
        if (!size_at_least(swap, MM_SWAP_MIN_BYTES) ||
            !size_at_least(swap, 1) ||
            (swap->last_lba - swap->first_lba + 1) > MM_SWAP_MAX_BYTES / 512)
            return "install.manual.mbr.swapSize";
    }
    return NULL;
}
