#define _GNU_SOURCE
#include "install/mbr_disk.h"
#include <fcntl.h>
#include <stdio.h>
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
    FILE *tmp=tmpfile();
    T(tmp != NULL);
    if (!tmp) return 1;
    int fd=fileno(tmp);
    const unsigned long long sectors=65536;
    T(ftruncate(fd, (off_t)(sectors*512)) == 0);

    unsigned char mbr[512]={0}, ebr[512]={0}, check[512];
    entry(mbr+446, 0x83, 2048, 16384);
    entry(mbr+462, 0x0f, 20480, 40960);
    mbr[510]=0x55; mbr[511]=0xaa;
    entry(ebr+446, 0x83, 1, 8192);
    ebr[510]=0x55; ebr[511]=0xaa;
    T(pwrite(fd,mbr,512,0)==512);
    T(pwrite(fd,ebr,512,(off_t)(20480*512))==512);

    MbrTable table; char tag[40];
    MbrStatus st=mbr_probe_fd(fd,sectors,512,&table,tag,sizeof tag);
    T(st==MBR_OK);
    T(table.primary_count==1 && table.primary[0].slot==1);
    T(table.logical_count==1 && table.logical[0].slot==5);
    T(strlen(tag)==16+1+5);
    T(pread(fd,check,512,0)==512 && !memcmp(check,mbr,512));

    char badtag[40];
    T(mbr_probe_fd(fd,sectors,4096,&table,badtag,sizeof badtag)==MBR_BAD_ARGUMENT);
    T(mbr_probe_fd(-1,sectors,512,&table,badtag,sizeof badtag)==MBR_BAD_ARGUMENT);
    T(mbr_probe_fd(fd,sectors,512,&table,badtag,4)==MBR_BAD_ARGUMENT);

    ebr[446+4]=0x07;
    T(pwrite(fd,ebr,512,(off_t)(20480*512))==512);
    T(mbr_probe_fd(fd,sectors,512,&table,badtag,sizeof badtag)==MBR_OK);
    T(strcmp(tag,badtag)!=0);
    fclose(tmp);
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
