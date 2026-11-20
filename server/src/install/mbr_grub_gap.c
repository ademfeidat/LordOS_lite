/* mbr_grub_gap.c — potential post-MBR embedding space; geometry only. */
#include "mbr_grub_gap.h"

#include <limits.h>

static int overlaps(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return a <= d && c <= b;
}

/* Do not let a malformed in-memory snapshot turn an unsafe range into an
 * apparently valid GRUB embedding gap. The parser also checks these rules,
 * but this public boundary validates its own input. */
static int snapshot_layout_valid(const DiskSnapshot *d) {
    if (d->has_extended != 0 && d->has_extended != 1) return 0;
    if (d->has_extended) {
        if (d->extended_first_lba < 2 ||
            d->extended_first_lba > d->extended_last_lba ||
            d->extended_last_lba >= d->total_sectors) return 0;
    } else if (d->extended_first_lba || d->extended_last_lba) {
        return 0;
    }

    for (unsigned i = 0; i < d->partition_count; i++) {
        const DsPartition *p = &d->partitions[i];
        if (!p->number || p->first_lba < 2 ||
            p->first_lba > p->last_lba ||
            p->last_lba >= d->total_sectors ||
            (p->is_logical != 0 && p->is_logical != 1)) return 0;
        if (p->is_logical) {
            if (p->number < 5 || !d->has_extended ||
                p->first_lba < d->extended_first_lba ||
                p->last_lba > d->extended_last_lba) return 0;
        } else {
            if (p->number > 4) return 0;
            if (d->has_extended &&
                overlaps(p->first_lba, p->last_lba,
                         d->extended_first_lba, d->extended_last_lba)) return 0;
        }
        for (unsigned j = 0; j < i; j++) {
            const DsPartition *q = &d->partitions[j];
            if (p->number == q->number ||
                overlaps(p->first_lba, p->last_lba,
                         q->first_lba, q->last_lba)) return 0;
        }
    }
    return 1;
}

MggResult mbr_grub_gap_check(const DiskSnapshot *d, uint64_t required_bytes,
                             uint64_t *available_sectors,
                             uint64_t *required_sectors) {
    if (available_sectors) *available_sectors = 0;
    if (required_sectors) *required_sectors = 0;
    if (!d || d->kind != DS_TABLE_MBR || d->logical_sector_size != 512 ||
        d->total_sectors < 2 || d->partition_count > DS_MAX_PARTITIONS ||
        !required_bytes || required_bytes > UINT64_MAX - 511)
        return MGG_BAD_SNAPSHOT;
    if (!snapshot_layout_valid(d)) return MGG_BAD_SNAPSHOT;

    uint64_t first = UINT64_MAX;
    if (d->has_extended) first = d->extended_first_lba;
    for (unsigned i = 0; i < d->partition_count; i++) {
        const DsPartition *p = &d->partitions[i];
        if (!p->is_logical && p->first_lba < first) first = p->first_lba;
    }
    if (first == UINT64_MAX || first < 2) return MGG_NO_PARTITION_GAP;

    uint64_t available = first - 1; /* LBA 1 through first-1; LBA 0 is MBR. */
    uint64_t required = (required_bytes + 511) / 512;
    if (available_sectors) *available_sectors = available;
    if (required_sectors) *required_sectors = required;
    return required <= available ? MGG_OK : MGG_TOO_SMALL;
}
