#define _GNU_SOURCE
#include "install/mbr_preflight.h"
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
    char path[]="/tmp/lordos-mbr-preflight-XXXXXX";
    int w=mkstemp(path); T(w>=0); if(w<0) return 1;
    const uint64_t sectors=24ull*1024*1024; /* sparse 12 GiB image */
    T(ftruncate(w,(off_t)(sectors*512))==0);
    unsigned char mbr[512]={0};
    mbr[446+4]=0x83; put32(mbr+446+8,2048); put32(mbr+446+12,20*1024*1024);
    mbr[510]=0x55; mbr[511]=0xaa;
    T(pwrite(w,mbr,512,0)==512); T(fsync(w)==0); close(w);
    int fd=open(path,O_RDONLY|O_CLOEXEC); T(fd>=0); if(fd<0) return 1;
    MbrTable parsed; char tag[40];
    T(mbr_probe_fd(fd,sectors,512,&parsed,tag,sizeof tag)==MBR_OK);
    MmPlan plan={.root_part=1,.root_format=1,.home_format=0,.swap_file_mib=0};
    const char *fs[DS_MAX_PARTITIONS+1]={0}; DiskSnapshot fresh;
    T(mbr_manual_preflight_fd(fd,sectors,512,tag,1,0,&plan,fs,&fresh)==NULL);
    T(fresh.kind==DS_TABLE_MBR && fresh.partition_count==1);
    T(mbr_manual_preflight_fd(fd,sectors,512,"0000000000000000-25165824",1,0,&plan,fs,&fresh)!=NULL);
    T(!strcmp(mbr_manual_preflight_fd(fd,sectors,512,tag,0,0,&plan,fs,&fresh),"install.manual.mbr.needBios"));
    T(!strcmp(mbr_manual_preflight_fd(fd,sectors,512,tag,1,1,&plan,fs,&fresh),"install.manual.mbr.busy"));
    T(!strcmp(mbr_manual_preflight_fd(fd,sectors,4096,tag,1,0,&plan,fs,&fresh),"install.manual.mbr.badTable"));
    plan.root_part=0;
    T(mbr_manual_preflight_fd(fd,sectors,512,tag,1,0,&plan,fs,&fresh)!=NULL);
    /* All preflight calls are read-only: sector zero must remain byte-identical. */
    unsigned char after[512];
    T(pread(fd,after,sizeof after,0)==(ssize_t)sizeof after);
    T(!memcmp(after,mbr,sizeof mbr));
    T((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY);
    close(fd); unlink(path);
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
