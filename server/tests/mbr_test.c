#include "install/mbr.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void put32(unsigned char *p, unsigned v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static void entry(unsigned char *s, unsigned slot, unsigned type, unsigned start, unsigned count, unsigned boot) {
    unsigned char *e=s+446+slot*16;
    memset(e,0,16); e[0]=(unsigned char)boot; e[4]=(unsigned char)type;
    put32(e+8,start); put32(e+12,count);
}
int main(void) {
    unsigned char s[512]; MbrTable t;
    memset(s,0,sizeof s); s[510]=0x55; s[511]=0xaa;
    T(mbr_parse_sector(s, 100000, &t)==MBR_OK && t.primary_count==0);
    T(mbr_parse_sector(s, 100000, NULL)==MBR_BAD_ARGUMENT);
    T(mbr_parse_sector(s, 1, &t)==MBR_BAD_ARGUMENT);
    s[510]=0; T(mbr_parse_sector(s,100000,&t)==MBR_BAD_SIGNATURE);
    s[510]=0x55;

    entry(s,0,0x83,2048,20000,0x80);
    entry(s,2,0x07,25000,10000,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_OK);
    T(t.primary_count==2 && t.primary[0].slot==1 && t.primary[1].slot==3);
    T(t.primary[0].bootable==1 && t.primary[0].first_lba==2048);

    entry(s,1,0x83,10000,1000,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_OVERLAP);
    memset(s+446,0,64); entry(s,0,0x83,99990,20,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_OUT_OF_RANGE);
    memset(s+446,0,64); entry(s,0,0x83,2048,100,1);
    T(mbr_parse_sector(s,100000,&t)==MBR_BAD_ENTRY);
    memset(s+446,0,64); entry(s,0,0x83,2048,100,0x80);
    entry(s,1,0x0f,4096,50000,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_OK && t.has_extended && t.extended.slot==2);
    entry(s,2,0x85,60000,1000,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_MULTIPLE_EXTENDED);
    memset(s+446,0,64); entry(s,0,0xee,1,99999,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_PROTECTIVE_GPT);
    entry(s,1,0x83,2048,100,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_HYBRID);
    memset(s+446,0,64); entry(s,0,0x83,2048,100,0);
    entry(s,1,0x0f,2048,500,0);
    T(mbr_parse_sector(s,100000,&t)==MBR_OVERLAP);

    printf("%u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
