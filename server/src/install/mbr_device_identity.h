/* mbr_device_identity.h — read-only proof that an fd is the selected whole disk. */
#ifndef LORD_INSTALL_MBR_DEVICE_IDENTITY_H
#define LORD_INSTALL_MBR_DEVICE_IDENTITY_H

#include <stdint.h>

typedef struct {
    uint32_t major_number;
    uint32_t minor_number;
    uint64_t disk_sectors; /* 512-byte sectors, matching the MBR parser */
    uint32_t logical_sector_size;
} MbrDeviceIdentity;

typedef enum {
    MDI_OK = 0,
    MDI_BAD_ARGUMENT = -1,
    MDI_NOT_READ_ONLY = -2,
    MDI_NOT_BLOCK_DEVICE = -3,
    MDI_GEOMETRY = -4,
    MDI_IO = -5,
    MDI_IDENTITY_MISMATCH = -6
} MdiResult;

/* Requires an already-open O_RDONLY whole-disk block-device descriptor.
 * Regular files are intentionally rejected here; regular-file images remain
 * supported only by isolated parser/backup tests, never by a production writer. */
MdiResult mbr_device_identity_probe_fd(int fd, MbrDeviceIdentity *out);

/* Compare all discovery identity fields, not just the path or disk size. */
int mbr_device_identity_matches(const MbrDeviceIdentity *observed,
                                const MbrDeviceIdentity *expected);

/* Probe and compare in one helper. On mismatch, out still receives observed facts. */
MdiResult mbr_device_identity_check_fd(int fd,
                                       const MbrDeviceIdentity *expected,
                                       MbrDeviceIdentity *out);

#endif
