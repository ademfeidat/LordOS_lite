/* disk_snapshot.h — read-only common view of GPT and DOS partition layouts.
 * This is a pure adapter: it performs no device I/O and never writes a partition table.
 * It is deliberately not wired into installer write paths yet. */
#ifndef LORD_DISK_SNAPSHOT_H
#define LORD_DISK_SNAPSHOT_H

#include <stdint.h>
#include "freespace.h"
#include "mbr.h"

#define DS_MAX_PARTITIONS FSP_MAX_PARTS

typedef enum { DS_TABLE_NONE, DS_TABLE_GPT, DS_TABLE_MBR } DsTableKind;

typedef struct {
    unsigned number;             /* GPT entry number, MBR primary slot, or logical number >= 5 */
    uint64_t first_lba, last_lba;/* inclusive */
    uint8_t type_code;            /* DOS type; zero for GPT */
    uint8_t is_esp;
    uint8_t is_logical;
} DsPartition;

typedef struct {
    DsTableKind kind;
    unsigned logical_sector_size;
    uint64_t total_sectors;
    unsigned partition_count;
    DsPartition partitions[DS_MAX_PARTITIONS];
    int has_extended;
    uint64_t extended_first_lba, extended_last_lba;
} DiskSnapshot;

/* Convert already-validated parser outputs into a bounded common representation.
 * Returns 0 on success, -1 on malformed or unrepresentable input. */
int ds_from_gpt(const FspTable *table, DiskSnapshot *out);
int ds_from_mbr(const MbrTable *table, uint64_t disk_sectors,
                unsigned logical_sector_size, DiskSnapshot *out);

#endif
