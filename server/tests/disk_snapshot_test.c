#include "install/disk_snapshot.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

int main(void) {
    DiskSnapshot s;
    FspTable g;
    memset(&g, 0, sizeof g);
    g.lsec = 512; g.total = 100000; g.first = 34; g.last = 99966; g.max_entries = 128;
    g.n = 2;
    g.p[0] = (FspPart){ .num=1, .start=2048, .end=4095, .esp=1 };
    g.p[1] = (FspPart){ .num=7, .start=8192, .end=16383, .esp=0 };
    T(ds_from_gpt(&g, &s) == 0 && s.kind == DS_TABLE_GPT);
    T(s.partition_count == 2 && s.partitions[1].number == 7 && s.partitions[0].is_esp);
    g.p[1].start = 3000;
    T(ds_from_gpt(&g, &s) != 0);

    MbrTable m;
    memset(&m, 0, sizeof m);
    m.primary_count = 1;
    m.primary[0] = (MbrPartition){ .type=0x83, .slot=1, .first_lba=2048, .sectors=2048 };
    m.has_extended = 1;
    m.extended = (MbrPartition){ .type=0x0f, .slot=2, .first_lba=10000, .sectors=50000 };
    m.logical_count = 2;
    m.logical[0] = (MbrPartition){ .type=0x83, .slot=5, .first_lba=10001, .sectors=1000 };
    m.logical[1] = (MbrPartition){ .type=0x07, .slot=6, .first_lba=12001, .sectors=1000 };
    T(ds_from_mbr(&m, 100000, 512, &s) == 0 && s.kind == DS_TABLE_MBR);
    T(s.partition_count == 3 && s.partitions[1].number == 5 && s.partitions[1].is_logical);
    m.logical[1].first_lba = 10500;
    T(ds_from_mbr(&m, 100000, 512, &s) != 0);
    m.logical[1].first_lba = 12001;
    m.logical[1].slot = 8;
    T(ds_from_mbr(&m, 100000, 512, &s) != 0);
    T(ds_from_mbr(&m, 100000, 1024, &s) != 0);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
