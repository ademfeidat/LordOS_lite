/* mbr_disk.h — read-only MBR/EBR probe for an already-open disk descriptor. */
#ifndef LORD_INSTALL_MBR_DISK_H
#define LORD_INSTALL_MBR_DISK_H

#include "mbr.h"
#include <stddef.h>
#include <stdint.h>

/* fd must be opened read-only. disk_sectors is measured in 512-byte sectors.
 * A stable fingerprint covers the MBR sector and every EBR sector traversed.
 * Returns an MbrStatus; MBR_OK means a complete DOS/MBR layout was read. */
MbrStatus mbr_probe_fd(int fd, uint64_t disk_sectors, unsigned logical_sector_size,
                       MbrTable *out, char *fingerprint, size_t fingerprint_size);

/* Strict comparison for the install-disks snapshot token and a fresh probe result.
 * This is stale-snapshot detection, not cryptographic authentication. */
int mbr_fingerprint_matches(const char *expected, const char *observed);

#endif
