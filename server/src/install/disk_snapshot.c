/* disk_snapshot.c — pure GPT/MBR snapshot adapters. */
#include "disk_snapshot.h"

#include <string.h>

static int valid_extent(uint64_t first, uint64_t last, uint64_t total) {
    return first <= last && last < total;
}

static int add_partition(DiskSnapshot *out, unsigned number, uint64_t first,
                         uint64_t last, uint8_t type, int esp, int logical) {
    if (out->partition_count >= DS_MAX_PARTITIONS || !number ||
        first > last) return -1;
    for (unsigned i = 0; i < out->partition_count; i++) {
        const DsPartition *p = &out->partitions[i];
        if (p->number == number ||
            (first <= p->last_lba && p->first_lba <= last)) return -1;
    }
    DsPartition *p = &out->partitions[out->partition_count++];
    p->number = number;
    p->first_lba = first;
    p->last_lba = last;
    p->type_code = type;
    p->is_esp = (uint8_t)!!esp;
    p->is_logical = (uint8_t)!!logical;
    return 0;
}

int ds_from_gpt(const FspTable *table, DiskSnapshot *out) {
    if (!table || !out || (table->lsec != 512 && table->lsec != 4096) ||
        !table->total || table->total > FSP_MAX_TOTAL ||
        table->n < 0 || table->n > FSP_MAX_PARTS ||
        !table->max_entries || table->max_entries > FSP_MAX_PARTS ||
        table->first > table->last || table->last >= table->total) return -1;
    memset(out, 0, sizeof *out);
    out->kind = DS_TABLE_GPT;
    out->logical_sector_size = table->lsec;
    out->total_sectors = table->total;
    for (int i = 0; i < table->n; i++) {
        const FspPart *p = &table->p[i];
        if (!p->num || p->num > table->max_entries ||
            !valid_extent(p->start, p->end, table->total) ||
            p->start < table->first || p->end > table->last ||
            add_partition(out, p->num, p->start, p->end, 0, p->esp, 0))
            return -1;
    }
    return 0;
}

int ds_from_mbr(const MbrTable *table, uint64_t disk_sectors,
                unsigned logical_sector_size, DiskSnapshot *out) {
    if (!table || !out || (logical_sector_size != 512 && logical_sector_size != 4096) ||
        disk_sectors < 2 || disk_sectors > FSP_MAX_TOTAL ||
        table->primary_count > MBR_PRIMARY_SLOTS ||
        table->logical_count > MBR_LOGICAL_MAX) return -1;
    memset(out, 0, sizeof *out);
    out->kind = DS_TABLE_MBR;
    out->logical_sector_size = logical_sector_size;
    out->total_sectors = disk_sectors;
    if (table->has_extended) {
        uint64_t first = table->extended.first_lba;
        uint64_t count = table->extended.sectors;
        if (!count || first >= disk_sectors || count > disk_sectors - first ||
            (table->extended.type != 0x05 && table->extended.type != 0x0f &&
             table->extended.type != 0x85)) return -1;
        out->has_extended = 1;
        out->extended_first_lba = first;
        out->extended_last_lba = first + count - 1;
    }
    for (unsigned i = 0; i < table->primary_count; i++) {
        const MbrPartition *p = &table->primary[i];
        if (!p->slot || p->slot > MBR_PRIMARY_SLOTS || !p->type ||
            !p->sectors || p->first_lba >= disk_sectors ||
            p->sectors > disk_sectors - p->first_lba ||
            add_partition(out, p->slot, p->first_lba,
                          (uint64_t)p->first_lba + p->sectors - 1,
                          p->type, 0, 0)) return -1;
        if (out->has_extended &&
            p->first_lba <= out->extended_last_lba &&
            out->extended_first_lba <= (uint64_t)p->first_lba + p->sectors - 1)
            return -1;
    }
    for (unsigned i = 0; i < table->logical_count; i++) {
        const MbrPartition *p = &table->logical[i];
        if (p->slot != i + 5 || !p->type || !p->sectors ||
            p->first_lba >= disk_sectors ||
            p->sectors > disk_sectors - p->first_lba ||
            !out->has_extended || p->first_lba < out->extended_first_lba ||
            (uint64_t)p->first_lba + p->sectors - 1 > out->extended_last_lba ||
            add_partition(out, p->slot, p->first_lba,
                          (uint64_t)p->first_lba + p->sectors - 1,
                          p->type, 0, 1)) return -1;
    }
    return 0;
}
