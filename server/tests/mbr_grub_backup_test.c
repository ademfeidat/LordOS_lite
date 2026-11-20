#define _GNU_SOURCE
#include "../src/install/mbr_grub_backup.h"
#include "../src/pkg/crypto.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DISK_SECTORS 131072u
#define TEST_RANGE_BYTES 58880u

static int failures;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; \
} } while (0)

static void fill_spec(MgbSpec *s) {
    memset(s, 0, sizeof *s);
    s->logical_sector_size = 512;
    s->disk_sectors = DISK_SECTORS;
    s->range_count = 1;
    s->ranges[0].offset = 0;
    s->ranges[0].length = TEST_RANGE_BYTES;
    strcpy(s->grub_version, "2.12-9+deb13u2");
    memset(s->grub_install_sha256, '1', 64);
    s->grub_install_sha256[64] = 0;
    memset(s->grub_bios_setup_sha256, '2', 64);
    s->grub_bios_setup_sha256[64] = 0;
    memset(s->modules_sha256, '3', 64);
    s->modules_sha256[64] = 0;
}

static void fill_snapshot(DiskSnapshot *d) {
    memset(d, 0, sizeof *d);
    d->kind = DS_TABLE_MBR;
    d->logical_sector_size = 512;
    d->total_sectors = DISK_SECTORS;
    d->partition_count = 1;
    d->partitions[0].number = 1;
    d->partitions[0].first_lba = 2048;
    d->partitions[0].last_lba = DISK_SECTORS - 1;
    d->partitions[0].type_code = 0x83;
}

static int write_pattern(int fd, unsigned char value, size_t length) {
    unsigned char buf[4096];
    memset(buf, value, sizeof buf);
    size_t off = 0;
    while (off < length) {
        size_t n = length - off < sizeof buf ? length - off : sizeof buf;
        ssize_t w = pwrite(fd, buf, n, (off_t)off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int range_is(int fd, unsigned char value, size_t length) {
    unsigned char buf[4096];
    size_t off = 0;
    while (off < length) {
        size_t n = length - off < sizeof buf ? length - off : sizeof buf;
        if (pread(fd, buf, n, (off_t)off) != (ssize_t)n) return 0;
        for (size_t i = 0; i < n; i++) if (buf[i] != value) return 0;
        off += n;
    }
    return 1;
}

int main(void) {
    char dir_template[] = "/tmp/lordos-mbr-backup-XXXXXX";
    char *dir = mkdtemp(dir_template);
    CHECK(dir != NULL);
    if (!dir) return 1;

    char disk_path[512], other_path[512];
    snprintf(disk_path, sizeof disk_path, "%s/disk.img", dir);
    snprintf(other_path, sizeof other_path, "%s/other.img", dir);
    int create_fd = open(disk_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(create_fd >= 0);
    if (create_fd < 0) return 1;
    CHECK(ftruncate(create_fd, (off_t)DISK_SECTORS * 512) == 0);
    CHECK(write_pattern(create_fd, 0xA5, TEST_RANGE_BYTES) == 0);
    CHECK(fsync(create_fd) == 0);
    close(create_fd);

    int disk_ro = open(disk_path, O_RDONLY);
    int disk_rw = open(disk_path, O_RDWR);
    int dir_fd = open(dir, O_RDONLY | O_DIRECTORY);
    CHECK(disk_ro >= 0 && disk_rw >= 0 && dir_fd >= 0);
    if (disk_ro < 0 || disk_rw < 0 || dir_fd < 0) return 1;

    DiskSnapshot snapshot;
    MgbSpec spec;
    fill_snapshot(&snapshot);
    fill_spec(&spec);

    CHECK(mgb_backup_fd(disk_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_OK);
    CHECK(mgb_verify_backup_fd(disk_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_OK);
    CHECK(mgb_backup_fd(disk_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_EXISTS);

    /* Simulate a failed GRUB write, then verify exact byte restoration. */
    CHECK(write_pattern(disk_rw, 0x5A, TEST_RANGE_BYTES) == 0);
    CHECK(fsync(disk_rw) == 0);
    CHECK(range_is(disk_rw, 0x5A, TEST_RANGE_BYTES));
    CHECK(mgb_restore_fd(disk_rw, dir_fd, "grub.backup", &snapshot, &spec) == MGB_OK);
    CHECK(range_is(disk_rw, 0xA5, TEST_RANGE_BYTES));

    /* A read-only descriptor must never be accepted for restoration. */
    CHECK(mgb_restore_fd(disk_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_BAD_INPUT);

    /* Invalid overlapping write ranges must fail before creating a backup. */
    MgbSpec invalid = spec;
    invalid.range_count = 2;
    invalid.ranges[1].offset = 256;
    invalid.ranges[1].length = 512;
    CHECK(mgb_backup_fd(disk_ro, dir_fd, "invalid.backup", &snapshot, &invalid) == MGB_BAD_INPUT);
    CHECK(faccessat(dir_fd, "invalid.backup", F_OK, 0) != 0);

    /* A backup is bound to the exact opened disk identity, not just its geometry. */
    int other_create = open(other_path, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(other_create >= 0);
    if (other_create >= 0) {
        CHECK(ftruncate(other_create, (off_t)DISK_SECTORS * 512) == 0);
        close(other_create);
        int other_ro = open(other_path, O_RDONLY);
        CHECK(other_ro >= 0);
        if (other_ro >= 0) {
            CHECK(mgb_verify_backup_fd(other_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_WRONG_DISK);
            close(other_ro);
        }
    }

    /* Tampering with the backup payload must be detected before restore. */
    int backup_rw = openat(dir_fd, "grub.backup", O_RDWR | O_CLOEXEC);
    CHECK(backup_rw >= 0);
    if (backup_rw >= 0) {
        unsigned char bad = 0x00;
        CHECK(pwrite(backup_rw, &bad, 1, MGB_BACKUP_HEADER_SIZE + 16) == 1);
        close(backup_rw);
        CHECK(mgb_verify_backup_fd(disk_ro, dir_fd, "grub.backup", &snapshot, &spec) == MGB_INTEGRITY);
        CHECK(mgb_restore_fd(disk_rw, dir_fd, "grub.backup", &snapshot, &spec) == MGB_INTEGRITY);
    }

    unlinkat(dir_fd, "grub.backup", 0);
    close(dir_fd);
    close(disk_ro);
    close(disk_rw);
    unlink(disk_path);
    unlink(other_path);
    unlinkat(AT_FDCWD, dir, AT_REMOVEDIR);

    if (failures) {
        fprintf(stderr, "%d MBR GRUB backup test(s) failed\n", failures);
        return 1;
    }
    puts("MBR GRUB backup/restore tests passed");
    return 0;
}
