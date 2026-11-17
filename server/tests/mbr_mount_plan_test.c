#include "install/mbr_mount_plan.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

int main(void) {
    DiskSnapshot d;
    memset(&d, 0, sizeof d);
    d.kind=DS_TABLE_MBR; d.logical_sector_size=512; d.total_sectors=100000000;
    d.partition_count=3;
    d.partitions[0]=(DsPartition){.number=1,.first_lba=2048,.last_lba=20973567,.type_code=0x83};
    d.partitions[1]=(DsPartition){.number=2,.first_lba=20973568,.last_lba=41945087,.type_code=0x83};
    d.partitions[2]=(DsPartition){.number=3,.first_lba=41945088,.last_lba=42469375,.type_code=0x82};
    MmPlan s={.root_part=1,.home_part=2,.swap_part=3,.root_format=1,.home_format=0};
    MpPlan p;
    const char *uuid="01234567-89ab-cdef-0123-456789abcdef";
    T(mp_build(&d,&s,1,0,"ext4",uuid,&p)==0);
    T(p.action_count==3 && p.actions[0].kind==MP_FORMAT_ROOT_EXT4);
    T(p.actions[1].kind==MP_MOUNT_ROOT && p.actions[2].kind==MP_MOUNT_HOME);
    T(p.fstab_count==2 && strstr(p.fstab[1],uuid)!=NULL);
    T(strstr(p.fstab[0],"@ROOT_UUID_AFTER_FORMAT@")!=NULL);
    T(mp_build(&d,&s,0,0,"ext4",uuid,&p)!=0);
    T(mp_build(&d,&s,1,1,"ext4",uuid,&p)!=0);
    T(mp_build(&d,&s,1,0,"ext4","bad-uuid",&p)!=0);
    T(mp_build(&d,&s,1,0,"unknown",uuid,&p)!=0);
    s.home_format=1;
    T(mp_build(&d,&s,1,0,NULL,NULL,&p)==0);
    T(p.action_count==4 && p.actions[2].kind==MP_FORMAT_HOME_EXT4);
    T(strstr(p.fstab[1],"@HOME_UUID_AFTER_FORMAT@")!=NULL);
    s.home_part=0; s.swap_part=3;
    T(mp_build(&d,&s,1,0,NULL,NULL,&p)==0);
    T(p.actions[2].kind==MP_MKSWAP_PARTITION);
    T(strstr(p.fstab[1],"@SWAP_UUID_AFTER_MKSWAP@")!=NULL);
    s.swap_part=0; s.swap_file_mib=1024;
    T(mp_build(&d,&s,1,0,NULL,NULL,&p)==0);
    T(p.actions[2].kind==MP_CREATE_SWAPFILE && p.actions[2].size_mib==1024);
    T(!strcmp(p.fstab[1],"/swapfile none swap sw 0 0"));
    s.swap_file_mib=16385; T(mp_build(&d,&s,1,0,NULL,NULL,&p)!=0);
    s.swap_file_mib=0; s.root_format=0; T(mp_build(&d,&s,1,0,NULL,NULL,&p)!=0);
    s.root_format=1; s.home_part=1; T(mp_build(&d,&s,1,0,NULL,NULL,&p)!=0);
    s.home_part=0; d.kind=DS_TABLE_GPT; T(mp_build(&d,&s,1,0,NULL,NULL,&p)!=0);
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
