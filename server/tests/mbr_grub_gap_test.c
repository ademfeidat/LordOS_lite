#define _GNU_SOURCE
#include "install/mbr_grub_gap.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

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
    /* The audited GRUB build writes 58,880 bytes (LBA 0..114). */
    T(mbr_grub_gap_check(&d,58880,&avail,&need)==MGG_TOO_SMALL);
    d.partitions[0].first_lba=256;
    T(mbr_grub_gap_check(&d,58880,&avail,&need)==MGG_OK);
    T(avail==255 && need==115);
    d.partitions[0].first_lba=63;
    T(mbr_grub_gap_check(&d,58880,&avail,&need)==MGG_TOO_SMALL);
    T(avail==62 && need==115);
    d.partitions[0].first_lba=2048;
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

    /* Content preflight ignores the MBR itself but rejects any non-zero byte
     * in the exact audited post-MBR write interval. */
    char image[]="/tmp/lordos-mbr-gap-XXXXXX";
    int w=mkstemp(image); T(w>=0);
    if(w>=0) {
        const uint64_t sectors=4096;
        T(ftruncate(w,(off_t)(sectors*512))==0);
        unsigned char mbr[512]; memset(mbr,0xA5,sizeof mbr);
        T(pwrite(w,mbr,sizeof mbr,0)==(ssize_t)sizeof mbr);
        T(fsync(w)==0);
        close(w);
        int ro=open(image,O_RDONLY|O_CLOEXEC); T(ro>=0);
        uint64_t bad_lba=999;
        if(ro>=0) {
            T(mbr_grub_gap_contents_check_fd(ro,sectors,58880,&bad_lba)==MGC_OK);
            T(bad_lba==0);
            close(ro);
        }
        int rw=open(image,O_RDWR|O_CLOEXEC); T(rw>=0);
        if(rw>=0) {
            unsigned char marker=0x7f;
            T(pwrite(rw,&marker,1,512+9)==1);
            T(fsync(rw)==0);
            ro=open(image,O_RDONLY|O_CLOEXEC);
            T(ro>=0);
            if(ro>=0) {
                T(mbr_grub_gap_contents_check_fd(ro,sectors,58880,&bad_lba)==MGC_OCCUPIED);
                T(bad_lba==1);
                close(ro);
            }
            marker=0;
            T(pwrite(rw,&marker,1,512+9)==1);
            marker=0x33;
            T(pwrite(rw,&marker,1,512+113*512+7)==1);
            T(fsync(rw)==0);
            ro=open(image,O_RDONLY|O_CLOEXEC);
            T(ro>=0);
            if(ro>=0) {
                T(mbr_grub_gap_contents_check_fd(ro,sectors,58880,&bad_lba)==MGC_OCCUPIED);
                T(bad_lba==114);
                close(ro);
            }
            T(mbr_grub_gap_contents_check_fd(rw,sectors,58880,&bad_lba)==MGC_BAD_INPUT);
            T(mbr_grub_gap_contents_check_fd(rw,sectors,0,&bad_lba)==MGC_BAD_INPUT);
            close(rw);
        }
        unlink(image);
    }

    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
