#define _GNU_SOURCE
#include "install/mbr_busy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];
static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)

static void put(const char *rel,const char *s) {
    char p[512]; snprintf(p,sizeof p,"%s/%s",root,rel);
    char *q=strrchr(p,'/'); if(q){*q=0; char cmd[600];snprintf(cmd,sizeof cmd,"mkdir -p '%s'",p);(void)system(cmd);*q='/';}
    FILE *f=fopen(p,"w"); if(f){fputs(s,f);fclose(f);}
}
static void dir(const char *rel) {
    char p[512],cmd[600];snprintf(p,sizeof p,"%s/%s",root,rel);
    snprintf(cmd,sizeof cmd,"mkdir -p '%s'",p);(void)system(cmd);
}
static void setup(void) {
    snprintf(root,sizeof root,"/tmp/mbr-busy-XXXXXX"); T(mkdtemp(root)!=NULL);
    put("sys/class/block/sda/dev","8:0\n"); dir("sys/class/block/sda/holders");
    put("sys/class/block/sda1/dev","8:1\n"); dir("sys/class/block/sda1/holders");
    put("sys/class/block/sda2/dev","8:2\n"); dir("sys/class/block/sda2/holders");
    put("proc/self/mountinfo","");
    put("proc/swaps","Filename Type Size Used Priority\n");
}
int main(void) {
    setup();
    DiskSnapshot d; memset(&d,0,sizeof d);
    d.kind=DS_TABLE_MBR; d.logical_sector_size=512; d.total_sectors=100000;
    d.partition_count=2;
    d.partitions[0].number=1; d.partitions[1].number=2;
    T(mbr_snapshot_busy_at(root,"sda",&d)==0);
    T(mbr_partition_busy_at(root,"sda",1)==0);
    T(mbr_partition_busy_at(root,"sda",2)==0);
    put("proc/self/mountinfo","30 1 8:1 / /mnt/data rw - ext4 /dev/sda1 rw\n");
    T(mbr_snapshot_busy_at(root,"sda",&d)==1);
    put("proc/self/mountinfo","30 1 0:99 / /mnt/btrfs rw - btrfs /dev/sda1 rw\n");
    T(mbr_snapshot_busy_at(root,"sda",&d)==1);
    T(mbr_partition_busy_at(root,"sda",1)==1);
    put("proc/self/mountinfo","");
    put("proc/swaps","Filename Type Size Used Priority\n/dev/sda2 partition 100 0 -2\n");
    T(mbr_snapshot_busy_at(root,"sda",&d)==1);
    put("proc/swaps","Filename Type Size Used Priority\n");
    dir("sys/class/block/sda1/holders/dm-0");
    T(mbr_snapshot_busy_at(root,"sda",&d)==1);
    T(mbr_partition_busy_at(root,"sda",1)==1);
    d.kind=DS_TABLE_GPT; T(mbr_snapshot_busy_at(root,"sda",&d)==1);
    d.kind=DS_TABLE_MBR;
    char cmd[400];snprintf(cmd,sizeof cmd,"rm -rf '%s'",root);(void)system(cmd);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
