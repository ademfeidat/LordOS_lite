/* mbr_manual.c — conservative manual partition selection for BIOS + DOS/MBR. */
#include "mbr_manual.h"

#include <string.h>

static const DsPartition *find_part(const DiskSnapshot *d, unsigned number) {
    for (unsigned i = 0; i < d->partition_count; i++)
        if (d->partitions[i].number == number) return &d->partitions[i];
    return NULL;
}

static int snapshot_valid(const DiskSnapshot *d) {
    if (!d || d->kind != DS_TABLE_MBR || d->logical_sector_size != 512 ||
        d->total_sectors < 2 || d->partition_count > DS_MAX_PARTITIONS ||
        (d->has_extended != 0 && d->has_extended != 1))
        return 0;
    if (d->has_extended) {
        if (d->extended_first_lba > d->extended_last_lba ||
            d->extended_last_lba >= d->total_sectors) return 0;
    } else if (d->extended_first_lba || d->extended_last_lba) return 0;

    for (unsigned i = 0; i < d->partition_count; i++) {
        const DsPartition *p = &d->partitions[i];
        if (!p->number || p->number > DS_MAX_PARTITIONS ||
            p->first_lba > p->last_lba || p->last_lba >= d->total_sectors ||
            p->is_esp || p->is_logical > 1 ||
            (p->is_logical ? p->number < 5 : p->number > 4))
            return 0;
        if (p->is_logical && (!d->has_extended ||
            p->first_lba < d->extended_first_lba ||
            p->last_lba > d->extended_last_lba)) return 0;
        if (!p->is_logical && d->has_extended &&
            p->first_lba <= d->extended_last_lba &&
            d->extended_first_lba <= p->last_lba) return 0;
        for (unsigned j = 0; j < i; j++) {
            const DsPartition *q = &d->partitions[j];
            if (p->number == q->number ||
                (p->first_lba <= q->last_lba && q->first_lba <= p->last_lba))
                return 0;
        }
    }
    return 1;
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
    if (!snapshot_valid(d)) return "install.manual.mbr.badTable";
    if (!f->bios_booted) return "install.manual.mbr.needBios";
    if (f->disk_busy) return "install.manual.mbr.busy";
    if (!p->root_part || p->root_part > DS_MAX_PARTITIONS ||
        p->home_part > DS_MAX_PARTITIONS || p->swap_part > DS_MAX_PARTITIONS ||
        p->root_format != 1 || (p->home_format != 0 && p->home_format != 1) ||
        p->root_part == p->home_part || p->root_part == p->swap_part ||
        (p->home_part && p->home_part == p->swap_part) ||
        p->swap_file_mib > 16384 ||
        (p->swap_part && p->swap_file_mib)) return "install.manual.mbr.badPlan";

    const DsPartition *root = find_part(d, p->root_part);
    if (!root) return "install.manual.mbr.noSuchPartition";
    if (root->type_code != 0x83)
        return "install.manual.mbr.rootType";
    if (!size_at_least(root, MM_ROOT_MIN_BYTES))
        return "install.manual.mbr.rootTooSmall";

    if (p->home_part) {
        const DsPartition *home = find_part(d, p->home_part);
        if (!home) return "install.manual.mbr.noSuchPartition";
        if (home->type_code != 0x83)
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
            (swap->last_lba - swap->first_lba + 1) > MM_SWAP_MAX_BYTES / 512)
            return "install.manual.mbr.swapSize";
    }
    return NULL;
}


int mm_plan_from_fields(long root_part, long home_part, long swap_part,
                        int root_format, int root_format_seen, int home_format,
                        long swap_file_mib, MmPlan *out) {
    if (!out || root_part <= 0 || root_part > DS_MAX_PARTITIONS ||
        home_part < 0 || home_part > DS_MAX_PARTITIONS ||
        swap_part < 0 || swap_part > DS_MAX_PARTITIONS ||
        !root_format_seen || root_format != 1 ||
        (home_format != 0 && home_format != 1) ||
        swap_file_mib < 0 || swap_file_mib > 16384 ||
        root_part == home_part || root_part == swap_part ||
        (home_part && home_part == swap_part) ||
        (swap_part && swap_file_mib))
        return -1;
    *out = (MmPlan) {
        .root_part = (unsigned)root_part,
        .home_part = (unsigned)home_part,
        .swap_part = (unsigned)swap_part,
        .root_format = root_format,
        .home_format = home_format,
        .swap_file_mib = (unsigned)swap_file_mib
    };
    return 0;
}
