/* mbr.h — read-only DOS/MBR sector parser. No disk I/O and no partition writes. */
#ifndef LORD_INSTALL_MBR_H
#define LORD_INSTALL_MBR_H

#include <stdint.h>

#define MBR_SECTOR_SIZE 512
#define MBR_PRIMARY_SLOTS 4
#define MBR_LOGICAL_MAX 124

typedef enum {
    MBR_OK = 0,
    MBR_BAD_ARGUMENT,
    MBR_BAD_SIGNATURE,
    MBR_PROTECTIVE_GPT,
    MBR_HYBRID,
    MBR_BAD_ENTRY,
    MBR_OUT_OF_RANGE,
    MBR_OVERLAP,
    MBR_MULTIPLE_EXTENDED,
    MBR_EBR_IO,
    MBR_EBR_CHAIN,
    MBR_EBR_LOOP
} MbrStatus;

typedef struct {
    uint8_t type;
    uint8_t bootable;
    uint8_t slot;       /* 1..4, original primary-table slot */
    uint32_t first_lba;
    uint32_t sectors;
} MbrPartition;

typedef struct {
    MbrPartition primary[MBR_PRIMARY_SLOTS];
    unsigned primary_count;
    int has_extended;
    MbrPartition extended;
    MbrPartition logical[MBR_LOGICAL_MAX];
    unsigned logical_count;
} MbrTable;

/* Parses one 512-byte MBR sector. disk_sectors is the whole device size in 512-byte
 * logical sectors. Empty entries are ignored. Logical partitions are not enumerated yet. */
typedef int (*MbrReadSector)(void *context, uint64_t lba, uint8_t out[MBR_SECTOR_SIZE]);

MbrStatus mbr_parse_sector(const uint8_t sector[MBR_SECTOR_SIZE],
                           uint64_t disk_sectors, MbrTable *out);

/* Follows and validates an extended-partition EBR chain using a caller-supplied read-only
 * sector reader. Logical partition numbers begin at 5. */
MbrStatus mbr_read_logicals(MbrTable *table, uint64_t disk_sectors,
                            MbrReadSector read_sector, void *context);

#endif
