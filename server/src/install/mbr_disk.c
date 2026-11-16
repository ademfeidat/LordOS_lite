/* mbr_disk.c — bounded, read-only MBR/EBR probe. */
#define _FILE_OFFSET_BITS 64
#include "mbr_disk.h"

#include <stdio.h>
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
        logical_sector_size != MBR_SECTOR_SIZE || disk_sectors < 2)
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
