#include "install/mbr_manual.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define T(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

int main(void) {
    DiskSnapshot d;
    memset(&d, 0, sizeof d);
    d.kind = DS_TABLE_MBR; d.logical_sector_size = 512; d.total_sectors = 100000000;
    d.partition_count = 3; d.has_extended = 1;
    d.extended_first_lba = 20973568; d.extended_last_lba = 42469375;
    d.partitions[0] = (DsPartition){ .number=1, .first_lba=2048, .last_lba=20973567, .type_code=0x83 };
    d.partitions[1] = (DsPartition){ .number=5, .first_lba=20973568, .last_lba=41945087, .type_code=0x83, .is_logical=1 };
    d.partitions[2] = (DsPartition){ .number=6, .first_lba=41945088, .last_lba=42469375, .type_code=0x82, .is_logical=1 };

    MmFacts f; memset(&f, 0, sizeof f); f.bios_booted=1; f.disk=&d;
    strcpy(f.fstype[5], "ext4");
    MmPlan p = { .root_part=1, .home_part=5, .swap_part=6, .root_format=1 };
    T(mm_decide(&f, &p) == NULL);
    p.root_format=0; T(mm_decide(&f, &p) != NULL); p.root_format=1;
    p.root_part=99; T(mm_decide(&f, &p) != NULL); p.root_part=1;
    p.home_part=1; T(mm_decide(&f, &p) != NULL); p.home_part=5;
    p.swap_file_mib=512; T(mm_decide(&f, &p) != NULL); p.swap_file_mib=0;
    f.bios_booted=0; T(mm_decide(&f, &p) != NULL); f.bios_booted=1;
    f.disk_busy=1; T(mm_decide(&f, &p) != NULL); f.disk_busy=0;
    p.home_format=0; strcpy(f.fstype[5], "unknown"); T(mm_decide(&f, &p) != NULL);
    strcpy(f.fstype[5], "ext4");
    d.partitions[0].type_code=0x07; T(mm_decide(&f, &p) != NULL); d.partitions[0].type_code=0x83;
    d.partitions[2].type_code=0x83; T(mm_decide(&f, &p) != NULL); d.partitions[2].type_code=0x82;
    d.kind=DS_TABLE_GPT; T(mm_decide(&f, &p) != NULL); d.kind=DS_TABLE_MBR;
    p.home_part=0; p.swap_part=0; p.root_part=1; T(mm_decide(&f, &p) == NULL);
    p.root_format=2; T(mm_decide(&f, &p) != NULL); p.root_format=1;
    p.home_format=2; T(mm_decide(&f, &p) != NULL); p.home_format=0;
    p.swap_file_mib=16385; T(mm_decide(&f, &p) != NULL); p.swap_file_mib=0;
    d.partitions[1].number=1; T(mm_decide(&f, &p) != NULL); d.partitions[1].number=5;
    d.partitions[1].first_lba=2048; T(mm_decide(&f, &p) != NULL); d.partitions[1].first_lba=20973568;
    d.extended_last_lba=d.total_sectors; T(mm_decide(&f, &p) != NULL); d.extended_last_lba=42469375;
    d.extended_first_lba=0; T(mm_decide(&f, &p) != NULL); d.extended_first_lba=20973568;
    d.partitions[0].first_lba=0; T(mm_decide(&f, &p) != NULL); d.partitions[0].first_lba=2048;
    d.extended_first_lba=42469376; T(mm_decide(&f, &p) != NULL); d.extended_first_lba=20973568;
    d.partitions[1].first_lba=42469376; T(mm_decide(&f, &p) != NULL); d.partitions[1].first_lba=20973568;
    d.partitions[1].is_logical=0; T(mm_decide(&f, &p) != NULL); d.partitions[1].is_logical=1;
    d.partitions[1].is_logical=2; T(mm_decide(&f, &p) != NULL); d.partitions[1].is_logical=1;
    d.has_extended=2; T(mm_decide(&f, &p) != NULL); d.has_extended=1;
    MmPlan converted;
    T(mm_plan_from_fields(1, 5, 6, 1, 1, 0, 0, &converted) == 0);
    T(converted.root_part == 1 && converted.home_part == 5 && converted.swap_part == 6);
    T(mm_plan_from_fields(1, 0, 0, 1, 0, 0, 0, &converted) != 0); /* omitted rootFormat */
    T(mm_plan_from_fields(1, 0, 0, 0, 1, 0, 0, &converted) != 0); /* root must format */
    T(mm_plan_from_fields(0, 0, 0, 1, 1, 0, 0, &converted) != 0);
    T(mm_plan_from_fields(1, 1, 0, 1, 1, 0, 0, &converted) != 0);
    T(mm_plan_from_fields(1, 0, 0, 1, 1, 2, 0, &converted) != 0);
    T(mm_plan_from_fields(1, 0, 0, 1, 1, 0, -1, &converted) != 0);
    T(mm_plan_from_fields(1, 0, 0, 1, 1, 0, 16385, &converted) != 0);
    T(mm_plan_from_fields(1, 0, 0, 1, 1, 0, 512, &converted) == 0);
    T(mm_plan_from_fields(1, 0, 6, 1, 1, 0, 512, &converted) != 0);
    T(mm_plan_from_fields(1, 0, 0, 1, 1, 0, 0, NULL) != 0);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
