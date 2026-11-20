#include "install/mbr_grub_gap.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)

int main(void) {
    DiskSnapshot d; memset(&d,0,sizeof d);
    d.kind=DS_TABLE_MBR; d.logical_sector_size=512; d.total_sectors=100000;
    d.partition_count=1;
    d.partitions[0]=(DsPartition){.number=1,.first_lba=2048,.last_lba=3000,.type_code=0x83};
    uint64_t avail=0,need=0;
    T(mbr_grub_gap_check(&d,1024*1024-512,&avail,&need)==MGG_OK);
    T(avail==2047 && need==2047);
    T(mbr_grub_gap_check(&d,1024*1024,&avail,&need)==MGG_TOO_SMALL);
    T(avail==2047 && need==2048);
    d.partitions[0].first_lba=63;
    T(mbr_grub_gap_check(&d,30000,&avail,&need)==MGG_OK);
    T(avail==62 && need==59);
    T(mbr_grub_gap_check(&d,32000,&avail,&need)==MGG_TOO_SMALL);
    d.partitions[0].first_lba=1;
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[0].first_lba=2048;

    d.has_extended=1; d.extended_first_lba=4096; d.extended_last_lba=7000;
    d.partition_count=2;
    d.partitions[1]=(DsPartition){.number=5,.first_lba=4097,.last_lba=6000,.type_code=0x83,.is_logical=1};
    T(mbr_grub_gap_check(&d,1024*1024-512,&avail,&need)==MGG_OK);
    T(avail==2047); /* first primary still determines the gap */
    d.partitions[0].first_lba=8192; d.partitions[0].last_lba=90000;
    T(mbr_grub_gap_check(&d,2*1024*1024,&avail,&need)==MGG_TOO_SMALL);
    T(avail==4095); /* the extended container begins at LBA 4096 */

    d.partitions[0].number=5; /* a logical number cannot describe a primary */
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[0].number=1;
    d.partitions[1].number=4; /* logical numbers start at 5 */
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[1].number=5;
    d.partitions[1].last_lba=7001; /* logical partition escapes its container */
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[1].last_lba=6000;
    d.partitions[0].first_lba=5000; /* primary overlaps the extended container */
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[0].first_lba=8192;

    d.logical_sector_size=4096;
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.logical_sector_size=512; d.total_sectors=100;
    d.has_extended=0; d.extended_first_lba=0; d.extended_last_lba=0;
    d.partition_count=1;
    d.partitions[0]=(DsPartition){.number=1,.first_lba=90,.last_lba=100,.type_code=0x83};
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);

    /* Duplicate numbers and overlapping primary/logical extents are rejected. */
    d.total_sectors=100000; d.partition_count=2;
    d.partitions[0]=(DsPartition){.number=1,.first_lba=2048,.last_lba=10000,.type_code=0x83};
    d.partitions[1]=(DsPartition){.number=1,.first_lba=12000,.last_lba=20000,.type_code=0x83};
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);
    d.partitions[1].number=2; d.partitions[1].first_lba=9000;
    T(mbr_grub_gap_check(&d,512,&avail,&need)==MGG_BAD_SNAPSHOT);

    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
