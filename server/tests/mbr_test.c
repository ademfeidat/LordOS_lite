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
typedef struct { uint64_t lba[2]; unsigned char sector[2][512]; unsigned count; } EbrFixture;
static int read_ebr(void *context, uint64_t lba, uint8_t out[512]) {
    EbrFixture *f = context;
    for (unsigned i = 0; i < f->count; i++)
        if (f->lba[i] == lba) { memcpy(out, f->sector[i], 512); return 0; }
    return -1;
}
static void ebr_entry(unsigned char *s, unsigned slot, unsigned type,
                      unsigned start, unsigned count) {
    entry(s, slot, type, start, count, 0);
}
static void ebr_tests(void) {
    unsigned char mbr[512] = {0};
    MbrTable t;
    EbrFixture f;
    memset(&f, 0, sizeof f);
    mbr[510] = 0x55; mbr[511] = 0xaa;
    entry(mbr, 0, 0x0f, 1000, 50000, 0);
    T(mbr_parse_sector(mbr, 100000, &t) == MBR_OK);
    f.count = 2; f.lba[0] = 1000; f.lba[1] = 3000;
    f.sector[0][510] = f.sector[1][510] = 0x55;
    f.sector[0][511] = f.sector[1][511] = 0xaa;
    ebr_entry(f.sector[0], 0, 0x83, 1, 1000);
    ebr_entry(f.sector[0], 1, 0x0f, 2000, 48000);
    ebr_entry(f.sector[1], 0, 0x07, 1, 2000);
    T(mbr_read_logicals(&t, 100000, read_ebr, &f) == MBR_OK);
    T(t.logical_count == 2 && t.logical[0].slot == 5 && t.logical[0].first_lba == 1001);
    T(t.logical[1].slot == 6 && t.logical[1].first_lba == 3001);

    ebr_entry(f.sector[1], 1, 0x0f, 2000, 48000);
    T(mbr_read_logicals(&t, 100000, read_ebr, &f) == MBR_EBR_LOOP);
    memset(f.sector[1], 0, 512);
    T(mbr_read_logicals(&t, 100000, read_ebr, &f) == MBR_EBR_CHAIN);
    T(mbr_read_logicals(&t, 100000, NULL, &f) == MBR_BAD_ARGUMENT);

    /* Reject malformed caller-supplied snapshots before any sector reads. */
    MbrTable bad = t;
    bad.has_extended = 1;
    bad.extended.type = 0x83;
    bad.extended.first_lba = 1000;
    bad.extended.sectors = 50000;
    T(mbr_read_logicals(&bad, 100000, read_ebr, &f) == MBR_OUT_OF_RANGE);
    bad = t;
    bad.has_extended = 1;
    bad.extended.type = 0x0f;
    bad.extended.first_lba = 99000;
    bad.extended.sectors = 2000;
    T(mbr_read_logicals(&bad, 100000, read_ebr, &f) == MBR_OUT_OF_RANGE);
    bad = t;
    bad.has_extended = 1;
    bad.extended.type = 0x0f;
    bad.extended.first_lba = 1000;
    bad.extended.sectors = 50000;
    bad.primary_count = MBR_PRIMARY_SLOTS + 1;
    T(mbr_read_logicals(&bad, 100000, read_ebr, &f) == MBR_BAD_ARGUMENT);
    bad = t;
    bad.has_extended = 1;
    bad.extended.type = 0x0f;
    bad.extended.first_lba = 1000;
    bad.extended.sectors = 50000;
    bad.primary_count = 1;
    bad.primary[0].type = 0x83;
    bad.primary[0].first_lba = 2000;
    bad.primary[0].sectors = 100;
    T(mbr_read_logicals(&bad, 100000, read_ebr, &f) == MBR_OVERLAP);

    f.count = 1; f.lba[0] = 1000;
    memset(f.sector[0], 0, 512); f.sector[0][510] = 0x55; f.sector[0][511] = 0xaa;
    ebr_entry(f.sector[0], 0, 0x83, 1, 60000);
    T(mbr_read_logicals(&t, 100000, read_ebr, &f) == MBR_EBR_CHAIN);
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

    ebr_tests();
    printf("%u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
