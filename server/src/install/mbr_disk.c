/* mbr_disk.c — bounded, read-only MBR/EBR probe. */
#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#include "mbr_disk.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    int fd;
    uint64_t hash;
} ProbeReader;

static void hash_sector(ProbeReader *r, const uint8_t *sector) {
    /* FNV-1a 64-bit. This is a change detector, not a cryptographic identity. */
    for (unsigned i = 0; i < MBR_SECTOR_SIZE; i++) {
        r->hash ^= sector[i];
        r->hash *= 1099511628211ull;
    }
}

static int read_sector(void *context, uint64_t lba, uint8_t out[MBR_SECTOR_SIZE]) {
    ProbeReader *r = context;
    if (lba > (uint64_t)INT64_MAX / MBR_SECTOR_SIZE) return -1;
    ssize_t n = pread(r->fd, out, MBR_SECTOR_SIZE, (off_t)(lba * MBR_SECTOR_SIZE));
    if (n != MBR_SECTOR_SIZE) return -1;
    hash_sector(r, out);
    return 0;
}

MbrStatus mbr_probe_fd(int fd, uint64_t disk_sectors, unsigned logical_sector_size,
                       MbrTable *out, char *fingerprint, size_t fingerprint_size) {
    if (fd < 0 || !out || !fingerprint || fingerprint_size < 40 ||
        logical_sector_size != MBR_SECTOR_SIZE || disk_sectors < 2 ||
        (fcntl(fd, F_GETFL) & O_ACCMODE) != O_RDONLY)
        return MBR_BAD_ARGUMENT;

    ProbeReader reader = { .fd = fd, .hash = 14695981039346656037ull };
    uint8_t sector[MBR_SECTOR_SIZE];
    if (read_sector(&reader, 0, sector)) return MBR_EBR_IO;
    MbrStatus status = mbr_parse_sector(sector, disk_sectors, out);
    if (status != MBR_OK) return status;
    status = mbr_read_logicals(out, disk_sectors, read_sector, &reader);
    if (status != MBR_OK) return status;
    snprintf(fingerprint, fingerprint_size, "%016llx-%llu",
             (unsigned long long)reader.hash, (unsigned long long)disk_sectors);
    return MBR_OK;
}


/* Compare only fingerprints in the exact format emitted above: 16 lowercase hex
 * digits, a hyphen, and the decimal sector count. The token detects stale snapshots;
 * it is deliberately not treated as a cryptographic identity. */
int mbr_fingerprint_matches(const char *expected, const char *observed) {
    if (!expected || !observed) return 0;
    size_t n = strlen(expected);
    if (n < 18 || n > 37 || strlen(observed) != n || expected[16] != '-') return 0;
    for (size_t i = 0; i < 16; i++)
        if (!((expected[i] >= '0' && expected[i] <= '9') ||
              (expected[i] >= 'a' && expected[i] <= 'f'))) return 0;
    if (expected[17] < '1' || expected[17] > '9') return 0;
    for (size_t i = 18; i < n; i++)
        if (expected[i] < '0' || expected[i] > '9') return 0;
    return strcmp(expected, observed) == 0;
}
