#define _GNU_SOURCE
#include "install/mbr_disk.h"
#include "install/disk_snapshot.h"
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
static void entry(unsigned char *e, unsigned type, unsigned start, unsigned count) {
    memset(e, 0, 16); e[4]=(unsigned char)type; put32(e+8,start); put32(e+12,count);
}

int main(void) {
    char path[]="/tmp/lordos-mbr-probe-XXXXXX";
    int wfd=mkstemp(path);
    T(wfd >= 0);
    if (wfd < 0) return 1;
    const unsigned long long sectors=65536;
    T(ftruncate(wfd, (off_t)(sectors*512)) == 0);

    unsigned char mbr[512]={0}, ebr[512]={0}, check[512];
    entry(mbr+446, 0x83, 2048, 16384);
    entry(mbr+462, 0x0f, 20480, 40960);
    mbr[510]=0x55; mbr[511]=0xaa;
    entry(ebr+446, 0x83, 1, 8192);
    ebr[510]=0x55; ebr[511]=0xaa;
    T(pwrite(wfd,mbr,512,0)==512);
    T(pwrite(wfd,ebr,512,(off_t)(20480*512))==512);
    T(fsync(wfd)==0);
    MbrTable write_table; char write_tag[40];
    T(mbr_probe_fd(wfd,sectors,512,&write_table,write_tag,sizeof write_tag)==MBR_BAD_ARGUMENT);
    T(close(wfd)==0);
    int fd=open(path,O_RDONLY|O_CLOEXEC); T(fd>=0);
    if(fd<0) return 1;

    MbrTable table; char tag[40];
    MbrStatus st=mbr_probe_fd(fd,sectors,512,&table,tag,sizeof tag);
    T(st==MBR_OK);
    T(table.primary_count==1 && table.primary[0].slot==1);
    T(table.logical_count==1 && table.logical[0].slot==5);
    DiskSnapshot snapshot;
    T(ds_from_mbr(&table,sectors,512,&snapshot)==0);
    T(snapshot.partition_count==2);
    T(snapshot.partitions[0].number==1 && snapshot.partitions[1].number==5);
    T(strlen(tag)==16+1+5);
    T(pread(fd,check,512,0)==512 && !memcmp(check,mbr,512));

    char badtag[40];
    T(mbr_probe_fd(fd,sectors,4096,&table,badtag,sizeof badtag)==MBR_BAD_ARGUMENT);
    T(mbr_probe_fd(-1,sectors,512,&table,badtag,sizeof badtag)==MBR_BAD_ARGUMENT);
    T(mbr_probe_fd(fd,sectors,512,&table,badtag,4)==MBR_BAD_ARGUMENT);

    T(close(fd)==0);
    wfd=open(path,O_WRONLY|O_CLOEXEC); T(wfd>=0);
    ebr[446+4]=0x07;
    T(pwrite(wfd,ebr,512,(off_t)(20480*512))==512);
    T(fsync(wfd)==0); T(close(wfd)==0);
    fd=open(path,O_RDONLY|O_CLOEXEC); T(fd>=0);
    T(mbr_probe_fd(fd,sectors,512,&table,badtag,sizeof badtag)==MBR_OK);
    T(strcmp(tag,badtag)!=0);
    close(fd); unlink(path);
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
