#include "install/mbr_grub_guard.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)

int main(void) {
    DiskSnapshot d; memset(&d,0,sizeof d);
    d.kind=DS_TABLE_MBR; d.logical_sector_size=512; d.total_sectors=100000;
    d.partition_count=1;
    d.partitions[0]=(DsPartition){.number=1,.first_lba=2048,.last_lba=90000,.type_code=0x83};
    const char *tag="0123456789abcdef-100000";
    MggGuardPlan p;
    T(mbr_grub_guard_plan(&d,1024*1024-512,0,0,tag,tag,1,&p)==MGG_GUARD_NEED_BIOS);
    T(mbr_grub_guard_plan(&d,1024*1024-512,1,1,tag,tag,1,&p)==MGG_GUARD_BUSY);
    T(mbr_grub_guard_plan(&d,1024*1024-512,1,0,tag,"fedcba9876543210-100000",1,&p)==MGG_GUARD_STALE);
    T(mbr_grub_guard_plan(&d,1024*1024-512,1,0,tag,tag,0,&p)==MGG_GUARD_CONFIRM_REPLACEMENT);
    T(mbr_grub_guard_plan(&d,1024*1024,1,0,tag,tag,1,&p)==MGG_GUARD_GAP);
    T(mbr_grub_guard_plan(&d,1024*1024-512,1,0,tag,tag,1,&p)==MGG_GUARD_OK);
    T(p.backup_first_lba==0 && p.backup_sector_count==2048);
    T(p.embedding_first_lba==1 && p.embedding_sector_count==2047);
    T(p.required_core_sectors==2047);
    T(p.existing_boot_manager_may_be_replaced==1 && p.writes_authorized==0);
    T(mbr_grub_guard_plan(&d,1,1,0,NULL,tag,1,&p)==MGG_GUARD_BAD_INPUT);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
