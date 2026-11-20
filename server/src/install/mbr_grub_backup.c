/* mbr_grub_backup.c — fail-closed backup and byte-verified rollback. */
#define _GNU_SOURCE
#include "mbr_grub_backup.h"
#include "../pkg/crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define H_MAGIC 0
#define H_VERSION 8
#define H_SIZE 12
#define H_SECTOR 16
#define H_RANGES 20
#define H_DISK_SECTORS 24
#define H_ST_DEV 32
#define H_ST_INO 40
#define H_ST_RDEV 48
#define H_ID_KIND 56
#define H_SNAPSHOT_HASH 64
#define H_GRUB_VERSION 96
#define H_GRUB_INSTALL 128
#define H_GRUB_SETUP 192
#define H_MODULES 256
#define H_RANGE_BASE 320
#define H_DATA_SIZE 384
#define H_DIGEST 480

static const uint8_t magic[8] = { 'L','G','R','U','B','B','K','1' };

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static int full_pread(int fd, void *buf, size_t n, uint64_t off) {
    uint8_t *p = buf;
    while (n) {
        ssize_t r = pread(fd, p, n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += (size_t)r; n -= (size_t)r; off += (uint64_t)r;
    }
    return 0;
}
static int full_pwrite(int fd, const void *buf, size_t n, uint64_t off) {
    const uint8_t *p = buf;
    while (n) {
        ssize_t r = pwrite(fd, p, n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += (size_t)r; n -= (size_t)r; off += (uint64_t)r;
    }
    return 0;
}
static int full_write(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += (size_t)r; n -= (size_t)r;
    }
    return 0;
}
static int valid_name(const char *s) {
    if (!s || !*s || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (; *s; s++)
        if (!( (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
               (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-'))
            return 0;
    return 1;
}
static int valid_hex64(const char *s) {
    if (!s || strnlen(s, 65) != 64) return 0;
    for (unsigned i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return 1;
}
static int spec_valid(const DiskSnapshot *d, const MgbSpec *s, uint64_t *data_size) {
    if (!d || !s || d->kind != DS_TABLE_MBR || d->logical_sector_size != 512 ||
        s->logical_sector_size != 512 || d->total_sectors != s->disk_sectors ||
        d->partition_count > DS_MAX_PARTITIONS || s->range_count == 0 ||
        s->range_count > MGB_MAX_RANGES || !s->disk_sectors ||
        strnlen(s->grub_version, sizeof s->grub_version) == sizeof s->grub_version ||
        !valid_hex64(s->grub_install_sha256) ||
        !valid_hex64(s->grub_bios_setup_sha256) || !valid_hex64(s->modules_sha256))
        return 0;
    if (s->disk_sectors > UINT64_MAX / 512) return 0;
    uint64_t disk_bytes = s->disk_sectors * 512, total = 0, previous_end = 0;
    /* pread/pwrite offsets are signed off_t values on the supported Linux targets.
     * Reject geometries that cannot be represented before any offset is cast. */
    if (disk_bytes > (uint64_t)INT64_MAX) return 0;
    for (unsigned i = 0; i < s->range_count; i++) {
        const MgbRange *r = &s->ranges[i];
        if (!r->length || r->offset % 512 || r->length % 512 ||
            r->offset > disk_bytes || r->length > disk_bytes - r->offset ||
            (i && r->offset < previous_end) || r->length > UINT64_MAX - total)
            return 0;
        previous_end = r->offset + r->length;
        total += r->length;
    }
    /* The backup payload starts after its fixed header; keep every backup-file
     * pread/pwrite offset representable by signed off_t as well. */
    if (total > (uint64_t)INT64_MAX - MGB_BACKUP_HEADER_SIZE) return 0;
    *data_size = total;
    return 1;
}
static void snapshot_hash(const DiskSnapshot *d, uint8_t out[32]) {
    Sha256 h; uint8_t b[32];
    sha256_init(&h);
    put32(b, (uint32_t)d->kind); sha256_update(&h, b, 4);
    put32(b, d->logical_sector_size); sha256_update(&h, b, 4);
    put64(b, d->total_sectors); sha256_update(&h, b, 8);
    put32(b, d->partition_count); sha256_update(&h, b, 4);
    put32(b, (uint32_t)d->has_extended); sha256_update(&h, b, 4);
    put64(b, d->extended_first_lba); sha256_update(&h, b, 8);
    put64(b, d->extended_last_lba); sha256_update(&h, b, 8);
    for (unsigned i = 0; i < d->partition_count; i++) {
        const DsPartition *p = &d->partitions[i];
        put32(b, p->number); sha256_update(&h, b, 4);
        put64(b, p->first_lba); sha256_update(&h, b, 8);
        put64(b, p->last_lba); sha256_update(&h, b, 8);
        b[0] = p->type_code; b[1] = p->is_esp; b[2] = p->is_logical;
        sha256_update(&h, b, 3);
    }
    sha256_final(&h, out);
}
static int disk_identity(int fd, uint64_t *dev, uint64_t *ino,
                         uint64_t *rdev, uint32_t *kind) {
    struct stat st;
    if (fstat(fd, &st)) return -1;
    if (S_ISBLK(st.st_mode)) {
        *dev = (uint64_t)st.st_dev; *ino = 0; *rdev = (uint64_t)st.st_rdev; *kind = 1;
        return 0;
    }
    if (S_ISREG(st.st_mode)) {
        *dev = (uint64_t)st.st_dev; *ino = (uint64_t)st.st_ino; *rdev = 0; *kind = 2;
        return 0;
    }
    return -1;
}
static int access_mode_is(int fd, int mode) {
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && (flags & O_ACCMODE) == mode;
}
static void build_header(uint8_t h[MGB_BACKUP_HEADER_SIZE],
                         const DiskSnapshot *d, const MgbSpec *s,
                         uint64_t dev, uint64_t ino, uint64_t rdev,
                         uint32_t id_kind, uint64_t data_size) {
    memset(h, 0, MGB_BACKUP_HEADER_SIZE);
    memcpy(h + H_MAGIC, magic, sizeof magic);
    put32(h + H_VERSION, 1);
    put32(h + H_SIZE, MGB_BACKUP_HEADER_SIZE);
    put32(h + H_SECTOR, s->logical_sector_size);
    put32(h + H_RANGES, s->range_count);
    put64(h + H_DISK_SECTORS, s->disk_sectors);
    put64(h + H_ST_DEV, dev); put64(h + H_ST_INO, ino); put64(h + H_ST_RDEV, rdev);
    put32(h + H_ID_KIND, id_kind);
    snapshot_hash(d, h + H_SNAPSHOT_HASH);
    memcpy(h + H_GRUB_VERSION, s->grub_version, strnlen(s->grub_version, sizeof s->grub_version));
    memcpy(h + H_GRUB_INSTALL, s->grub_install_sha256, 64);
    memcpy(h + H_GRUB_SETUP, s->grub_bios_setup_sha256, 64);
    memcpy(h + H_MODULES, s->modules_sha256, 64);
    for (unsigned i = 0; i < s->range_count; i++) {
        put64(h + H_RANGE_BASE + i * 16, s->ranges[i].offset);
        put64(h + H_RANGE_BASE + i * 16 + 8, s->ranges[i].length);
    }
    put64(h + H_DATA_SIZE, data_size);
}
static int header_matches(const uint8_t h[MGB_BACKUP_HEADER_SIZE],
                          const DiskSnapshot *d, const MgbSpec *s,
                          uint64_t dev, uint64_t ino, uint64_t rdev,
                          uint32_t id_kind, uint64_t data_size) {
    uint8_t expected[MGB_BACKUP_HEADER_SIZE];
    build_header(expected, d, s, dev, ino, rdev, id_kind, data_size);
    return !memcmp(h, expected, H_DIGEST);
}
static int verify_fd(int backup_fd, int disk_fd, const DiskSnapshot *d,
                     const MgbSpec *s, uint8_t header[MGB_BACKUP_HEADER_SIZE]) {
    uint64_t data_size, dev, ino, rdev;
    uint32_t id_kind;
    struct stat st;
    if (!spec_valid(d, s, &data_size) || disk_identity(disk_fd, &dev, &ino, &rdev, &id_kind) ||
        fstat(backup_fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        st.st_size < MGB_BACKUP_HEADER_SIZE ||
        (uint64_t)st.st_size != MGB_BACKUP_HEADER_SIZE + data_size)
        return MGB_BAD_BACKUP;
    if (full_pread(backup_fd, header, MGB_BACKUP_HEADER_SIZE, 0)) return MGB_IO;
    if (!header_matches(header, d, s, dev, ino, rdev, id_kind, data_size))
        return MGB_WRONG_DISK;
    uint8_t expected[32], actual[32], buf[65536];
    memcpy(expected, header + H_DIGEST, 32);
    memset(header + H_DIGEST, 0, 32);
    Sha256 h; sha256_init(&h); sha256_update(&h, header, MGB_BACKUP_HEADER_SIZE);
    uint64_t off = MGB_BACKUP_HEADER_SIZE, left = data_size;
    while (left) {
        size_t n = left > sizeof buf ? sizeof buf : (size_t)left;
        if (full_pread(backup_fd, buf, n, off)) return MGB_IO;
        sha256_update(&h, buf, n); off += n; left -= n;
    }
    sha256_final(&h, actual);
    memcpy(header + H_DIGEST, expected, 32);
    return memcmp(expected, actual, 32) ? MGB_INTEGRITY : MGB_OK;
}
static MgbResult open_backup(int dir_fd, const char *name, int *fd_out) {
    if (!valid_name(name) || dir_fd < 0 || !fd_out) return MGB_BAD_INPUT;
    int fd = openat(dir_fd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ELOOP ? MGB_BAD_BACKUP : MGB_IO;
    *fd_out = fd; return MGB_OK;
}

MgbResult mgb_backup_fd(int disk_fd, int dir_fd, const char *name,
                        const DiskSnapshot *d, const MgbSpec *s) {
    uint64_t data_size, dev, ino, rdev; uint32_t id_kind;
    if (!access_mode_is(disk_fd, O_RDONLY) || !spec_valid(d, s, &data_size) ||
        disk_identity(disk_fd, &dev, &ino, &rdev, &id_kind) ||
        !valid_name(name) || dir_fd < 0)
        return MGB_BAD_INPUT;
    uint8_t header[MGB_BACKUP_HEADER_SIZE], buf[65536];
    build_header(header, d, s, dev, ino, rdev, id_kind, data_size);
    int fd = openat(dir_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return errno == EEXIST ? MGB_EXISTS : MGB_IO;
    int failed = fchmod(fd, 0600) || full_write(fd, header, sizeof header);
    Sha256 h; sha256_init(&h); sha256_update(&h, header, sizeof header);
    for (unsigned i = 0; !failed && i < s->range_count; i++) {
        uint64_t pos = 0;
        while (pos < s->ranges[i].length) {
            size_t n = s->ranges[i].length - pos > sizeof buf ? sizeof buf :
                       (size_t)(s->ranges[i].length - pos);
            if (full_pread(disk_fd, buf, n, s->ranges[i].offset + pos) ||
                full_write(fd, buf, n)) { failed = 1; break; }
            sha256_update(&h, buf, n); pos += n;
        }
    }
    uint8_t digest[32];
    if (!failed) {
        sha256_final(&h, digest);
        memcpy(header + H_DIGEST, digest, 32);
        if (full_pwrite(fd, header, sizeof header, 0) || fsync(fd)) failed = 1;
    }
    if (close(fd)) failed = 1;
    if (!failed && fsync(dir_fd)) failed = 1;
    if (failed) {
        (void)unlinkat(dir_fd, name, 0); (void)fsync(dir_fd);
        return MGB_IO;
    }
    return mgb_verify_backup_fd(disk_fd, dir_fd, name, d, s);
}

MgbResult mgb_verify_backup_fd(int disk_fd, int dir_fd, const char *name,
                               const DiskSnapshot *d, const MgbSpec *s) {
    int fd = -1; MgbResult r = open_backup(dir_fd, name, &fd);
    if (r != MGB_OK) return r;
    uint8_t header[MGB_BACKUP_HEADER_SIZE];
    r = verify_fd(fd, disk_fd, d, s, header);
    close(fd);
    return r;
}

MgbResult mgb_restore_fd(int disk_fd, int dir_fd, const char *name,
                         const DiskSnapshot *d, const MgbSpec *s) {
    if (!access_mode_is(disk_fd, O_RDWR)) return MGB_BAD_INPUT;
    int fd = -1; MgbResult r = open_backup(dir_fd, name, &fd);
    if (r != MGB_OK) return r;
    uint8_t header[MGB_BACKUP_HEADER_SIZE];
    r = verify_fd(fd, disk_fd, d, s, header);
    if (r != MGB_OK) { close(fd); return r; }

    uint8_t backup[65536], current[65536];
    uint64_t file_off = MGB_BACKUP_HEADER_SIZE;
    for (unsigned i = 0; i < s->range_count; i++) {
        uint64_t pos = 0;
        while (pos < s->ranges[i].length) {
            size_t n = s->ranges[i].length - pos > sizeof backup ? sizeof backup :
                       (size_t)(s->ranges[i].length - pos);
            if (full_pread(fd, backup, n, file_off) ||
                full_pwrite(disk_fd, backup, n, s->ranges[i].offset + pos)) {
                close(fd); return MGB_IO;
            }
            file_off += n; pos += n;
        }
    }
    if (fsync(disk_fd)) { close(fd); return MGB_IO; }

    file_off = MGB_BACKUP_HEADER_SIZE;
    for (unsigned i = 0; i < s->range_count; i++) {
        uint64_t pos = 0;
        while (pos < s->ranges[i].length) {
            size_t n = s->ranges[i].length - pos > sizeof backup ? sizeof backup :
                       (size_t)(s->ranges[i].length - pos);
            if (full_pread(fd, backup, n, file_off) ||
                full_pread(disk_fd, current, n, s->ranges[i].offset + pos)) {
                close(fd); return MGB_IO;
            }
            if (memcmp(backup, current, n)) { close(fd); return MGB_INTEGRITY; }
            file_off += n; pos += n;
        }
    }
    close(fd);
    return MGB_OK;
}

MgbResult mgb_backup_digest_at(int dir_fd,const char *name,char out_hex[65]) {
    if(dir_fd<0 || !valid_name(name) || !out_hex) return MGB_BAD_INPUT;
    int fd=openat(dir_fd,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return errno==ELOOP ? MGB_BAD_BACKUP : MGB_IO;
    struct stat before,after;
    if(fstat(fd,&before) || !S_ISREG(before.st_mode) || before.st_nlink!=1 ||
       before.st_uid!=geteuid() || (before.st_mode&0777)!=0600 ||
       before.st_size<(off_t)MGB_BACKUP_HEADER_SIZE) {
        close(fd); return MGB_BAD_BACKUP;
    }
    Sha256 hash; sha256_init(&hash);
    uint8_t buf[65536];
    off_t offset=0;
    while(offset<before.st_size) {
        size_t want=(uint64_t)(before.st_size-offset)>sizeof buf ?
                    sizeof buf : (size_t)(before.st_size-offset);
        ssize_t got=pread(fd,buf,want,offset);
        if(got<0 && errno==EINTR) continue;
        if(got<=0) { close(fd); return MGB_IO; }
        sha256_update(&hash,buf,(size_t)got);
        offset+=(off_t)got;
    }
    if(fstat(fd,&after) || after.st_dev!=before.st_dev ||
       after.st_ino!=before.st_ino || after.st_size!=before.st_size ||
       after.st_mtim.tv_sec!=before.st_mtim.tv_sec ||
       after.st_mtim.tv_nsec!=before.st_mtim.tv_nsec ||
       after.st_ctim.tv_sec!=before.st_ctim.tv_sec ||
       after.st_ctim.tv_nsec!=before.st_ctim.tv_nsec) {
        close(fd); return MGB_INTEGRITY;
    }
    if(close(fd)) return MGB_IO;
    uint8_t digest[32];
    sha256_final(&hash,digest);
    hex_encode(digest,sizeof digest,out_hex);
    return MGB_OK;
}
