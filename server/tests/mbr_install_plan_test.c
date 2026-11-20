#include "install/mbr_install_plan.h"
#include "install/mbr_disk.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void put32(unsigned char *p, unsigned v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
int main(void) {
    char path[]="/tmp/lordos-mbr-plan-XXXXXX";
    int w=mkstemp(path); T(w>=0); if(w<0) return 1;
    const uint64_t sectors=24ull*1024*1024;
    T(ftruncate(w,(off_t)(sectors*512))==0);
    unsigned char mbr[512]={0};
    mbr[446+4]=0x83; put32(mbr+446+8,2048); put32(mbr+446+12,18*1024*1024);
    mbr[462+4]=0x83; put32(mbr+462+8,19000000); put32(mbr+462+12,2097152);
    mbr[478+4]=0x82; put32(mbr+478+8,21200000); put32(mbr+478+12,1048576);
    mbr[510]=0x55; mbr[511]=0xaa;
    T(pwrite(w,mbr,512,0)==512); T(fsync(w)==0); close(w);
    int fd=open(path,O_RDONLY|O_CLOEXEC); T(fd>=0); if(fd<0) return 1;
    MbrTable table; char tag[40];
    T(mbr_probe_fd(fd,sectors,512,&table,tag,sizeof tag)==MBR_OK);

    const char *fs[DS_MAX_PARTITIONS+1]={0};
    MipPlan plan;
    MmPlan selection={.root_part=1,.root_format=1,.home_format=0};
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,&plan)==NULL);
    T(plan.disk.kind==DS_TABLE_MBR);
    T(plan.mounts.action_count==2 && plan.mounts.actions[0].kind==MP_FORMAT_ROOT_EXT4);
    T(plan.grub.writes_authorized==0 && plan.writes_authorized==0);
    MbrDeviceIdentity expected_disk={.major_number=8,.minor_number=0,.disk_sectors=sectors,.logical_sector_size=512,.kernel_name="sda"};
    T(!strcmp(mbr_install_plan_build_block_fd(fd,&expected_disk,tag,1,
                                               &selection,fs,NULL,58880,58880,1,&plan),
              "install.manual.mbr.deviceIdentity"));

    selection.home_part=2;
    fs[2]="ext4";
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                        58880,58880,1,&plan),"install.manual.mbr.mountPlan"));
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,
                                "123e4567-e89b-12d3-a456-426614174000",
                                58880,58880,1,&plan)==NULL);
    T(plan.mounts.action_count==3 && plan.mounts.actions[2].kind==MP_MOUNT_HOME);
    fs[2]="unknown";
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,
                                        "123e4567-e89b-12d3-a456-426614174000",
                                        58880,58880,1,&plan),"install.manual.mbr.homeUnknownFs"));
    fs[2]="ext4";
    selection.home_format=1;
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,&plan)==NULL);
    T(plan.mounts.actions[2].kind==MP_FORMAT_HOME_EXT4);
    selection.home_format=0; selection.home_part=0;

    selection.swap_part=3;
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,&plan)==NULL);
    T(plan.mounts.actions[2].kind==MP_MKSWAP_PARTITION);
    selection.swap_part=0;

    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,0,0,&selection,fs,NULL,
                                        58880,58880,1,&plan),"install.manual.mbr.needBios"));
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,1,1,&selection,fs,NULL,
                                        58880,58880,1,&plan),"install.manual.mbr.busy"));
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,"0000000000000000-25165824",1,0,
                                        &selection,fs,NULL,58880,58880,1,&plan),
             "install.manual.mbr.diskChanged"));
    T(mbr_install_plan_build_fd(fd,sectors,4096,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,&plan)!=NULL);
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                        58880,58880,0,&plan),"install.manual.mbr.confirmBootloader"));
    T(!strcmp(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                        1024*1024,58880,1,&plan),"install.manual.mbr.noGrubGap"));
    selection.root_part=0;
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,&plan)!=NULL);
    selection.root_part=1;
    T(mbr_install_plan_build_fd(fd,sectors,512,tag,1,0,&selection,fs,NULL,
                                58880,58880,1,NULL)!=NULL);
    unsigned char after[512];
    T(pread(fd,after,sizeof after,0)==(ssize_t)sizeof after);
    T(!memcmp(after,mbr,sizeof mbr));
    T((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY);
    close(fd); unlink(path);
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
