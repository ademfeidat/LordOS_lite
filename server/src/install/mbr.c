/* mbr.c — deliberately read-only DOS/MBR parser. */
#include "mbr.h"

#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int extended_type(uint8_t type) {
    return type == 0x05 || type == 0x0f || type == 0x85;
}

MbrStatus mbr_parse_sector(const uint8_t sector[MBR_SECTOR_SIZE],
                           uint64_t disk_sectors, MbrTable *out) {
    if (!sector || !out || disk_sectors < 2) return MBR_BAD_ARGUMENT;
    memset(out, 0, sizeof *out);
    if (sector[510] != 0x55 || sector[511] != 0xaa) return MBR_BAD_SIGNATURE;

    unsigned protective = 0, nonempty = 0;
    for (unsigned i = 0; i < MBR_PRIMARY_SLOTS; i++) {
        const uint8_t *e = sector + 446 + i * 16;
        uint8_t boot = e[0], type = e[4];
        uint32_t first = le32(e + 8), count = le32(e + 12);
        if (!type && !first && !count && !boot) continue;
        nonempty++;
        if (boot != 0 && boot != 0x80) return MBR_BAD_ENTRY;
        if (!type || !first || !count) return MBR_BAD_ENTRY;
        if ((uint64_t)first >= disk_sectors ||
            (uint64_t)count > disk_sectors - first ||
            (uint64_t)count - 1 > UINT32_MAX - (uint64_t)first) return MBR_OUT_OF_RANGE;
        if (type == 0xee) protective++;

        MbrPartition p = {
            .type = type,
            .bootable = (uint8_t)(boot == 0x80),
            .slot = (uint8_t)(i + 1),
            .first_lba = first,
            .sectors = count
        };
        if (extended_type(type)) {
            if (out->has_extended) return MBR_MULTIPLE_EXTENDED;
            out->has_extended = 1;
            out->extended = p;
        } else {
            out->primary[out->primary_count++] = p;
        }
    }

    if (protective)
        return nonempty == 1 && protective == 1 ? MBR_PROTECTIVE_GPT : MBR_HYBRID;

    for (unsigned i = 0; i < out->primary_count; i++) {
        const MbrPartition *a = &out->primary[i];
        uint64_t a_end = (uint64_t)a->first_lba + a->sectors;
        if (out->has_extended) {
            uint64_t x_start = out->extended.first_lba;
            uint64_t x_end = x_start + out->extended.sectors;
            if ((uint64_t)a->first_lba < x_end && x_start < a_end)
                return MBR_OVERLAP;
        }
        for (unsigned j = i + 1; j < out->primary_count; j++) {
            const MbrPartition *b = &out->primary[j];
            uint64_t b_end = (uint64_t)b->first_lba + b->sectors;
            if ((uint64_t)a->first_lba < b_end &&
                (uint64_t)b->first_lba < a_end) return MBR_OVERLAP;
        }
    }
    return MBR_OK;
}


static int entry_empty(const uint8_t *e) {
    for (unsigned i = 0; i < 16; i++) if (e[i]) return 0;
    return 1;
}

static int extent_inside(uint64_t start, uint64_t count,
                         uint64_t lower, uint64_t upper) {
    return count && start >= lower && start <= upper && count - 1 <= upper - start;
}

static int extent_overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    return a < b + bn && b < a + an;
}

MbrStatus mbr_read_logicals(MbrTable *table, uint64_t disk_sectors,
                            MbrReadSector read_sector, void *context) {
    if (!table || disk_sectors < 2) return MBR_BAD_ARGUMENT;
    table->logical_count = 0;
    if (!table->has_extended) return MBR_OK;
    if (!read_sector) return MBR_BAD_ARGUMENT;

    const uint64_t base = table->extended.first_lba;
    const uint64_t limit = base + table->extended.sectors - 1;
    uint64_t ebr_lba = base;
    uint64_t visited[MBR_LOGICAL_MAX];
    unsigned visited_count = 0;
    uint8_t sector[MBR_SECTOR_SIZE];

    for (;;) {
        if (visited_count >= MBR_LOGICAL_MAX) return MBR_EBR_CHAIN;
        for (unsigned i = 0; i < visited_count; i++)
            if (visited[i] == ebr_lba) return MBR_EBR_LOOP;
        if (!extent_inside(ebr_lba, 1, base, limit) || ebr_lba > UINT32_MAX)
            return MBR_EBR_CHAIN;
        for (unsigned i = 0; i < table->logical_count; i++)
            if (extent_overlaps(ebr_lba, 1, table->logical[i].first_lba,
                                table->logical[i].sectors)) return MBR_OVERLAP;
        visited[visited_count++] = ebr_lba;
        if (read_sector(context, ebr_lba, sector)) return MBR_EBR_IO;
        if (sector[510] != 0x55 || sector[511] != 0xaa) return MBR_EBR_CHAIN;

        const uint8_t *data = sector + 446;
        const uint8_t *link = data + 16;
        if (!entry_empty(data)) {
            uint8_t boot = data[0], type = data[4];
            uint32_t relative = le32(data + 8), count = le32(data + 12);
            if ((boot != 0 && boot != 0x80) || !type || !relative || !count ||
                extended_type(type) || type == 0xee) return MBR_EBR_CHAIN;
            uint64_t absolute = ebr_lba + relative;
            if (absolute < ebr_lba || absolute > UINT32_MAX ||
                (uint64_t)count - 1 > UINT32_MAX - absolute ||
                !extent_inside(absolute, count, base, limit) ||
                absolute == ebr_lba) return MBR_EBR_CHAIN;
            for (unsigned i = 0; i < table->primary_count; i++)
                if (extent_overlaps(absolute, count, table->primary[i].first_lba,
                                    table->primary[i].sectors)) return MBR_OVERLAP;
            for (unsigned i = 0; i < table->logical_count; i++)
                if (extent_overlaps(absolute, count, table->logical[i].first_lba,
                                    table->logical[i].sectors)) return MBR_OVERLAP;
            MbrPartition p = {
                .type = type,
                .bootable = (uint8_t)(boot == 0x80),
                .slot = (uint8_t)(5 + table->logical_count),
                .first_lba = (uint32_t)absolute,
                .sectors = count
            };
            table->logical[table->logical_count++] = p;
        }

        if (entry_empty(link)) {
            if (!entry_empty(link + 16) || !entry_empty(link + 32)) return MBR_EBR_CHAIN;
            return MBR_OK;
        }

        uint8_t link_boot = link[0], link_type = link[4];
        uint32_t link_relative = le32(link + 8), link_count = le32(link + 12);
        if (link_boot || !extended_type(link_type) || !link_relative || !link_count)
            return MBR_EBR_CHAIN;
        uint64_t next = base + link_relative;
        if (next < base || next > UINT32_MAX ||
            (uint64_t)link_count - 1 > UINT32_MAX - next ||
            !extent_inside(next, link_count, base, limit))
            return MBR_EBR_CHAIN;
        if (!entry_empty(link + 16) || !entry_empty(link + 32)) return MBR_EBR_CHAIN;
        ebr_lba = next;
    }
}
