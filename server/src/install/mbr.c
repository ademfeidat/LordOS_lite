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
            (uint64_t)count > disk_sectors - first) return MBR_OUT_OF_RANGE;
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
