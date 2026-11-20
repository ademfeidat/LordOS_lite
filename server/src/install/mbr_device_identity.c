/* mbr_device_identity.c — read-only whole-disk block-device identity check. */
#include "mbr_device_identity.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

MdiResult mbr_device_identity_probe_fd(int fd, MbrDeviceIdentity *out) {
    if (fd < 0 || !out) return MDI_BAD_ARGUMENT;
    int flags=fcntl(fd,F_GETFL);
    if (flags<0) return MDI_IO;
    if ((flags&O_ACCMODE)!=O_RDONLY) return MDI_NOT_READ_ONLY;
    struct stat st;
    if (fstat(fd,&st)) return MDI_IO;
    if (!S_ISBLK(st.st_mode)) return MDI_NOT_BLOCK_DEVICE;

    uint32_t maj=(uint32_t)major(st.st_rdev);
    uint32_t min=(uint32_t)minor(st.st_rdev);
    char sysfs_path[128];
    int n=snprintf(sysfs_path,sizeof sysfs_path,"/sys/dev/block/%u:%u",maj,min);
    if(n<0 || (size_t)n>=sizeof sysfs_path) return MDI_IO;
    struct stat sysfs_st;
    if(stat(sysfs_path,&sysfs_st)) return MDI_IO; /* no sysfs proof: fail closed */
    n=snprintf(sysfs_path,sizeof sysfs_path,"/sys/dev/block/%u:%u/partition",maj,min);
    if(n<0 || (size_t)n>=sizeof sysfs_path) return MDI_IO;
    if(!lstat(sysfs_path,&sysfs_st)) return MDI_NOT_WHOLE_DISK;
    if(errno!=ENOENT) return MDI_IO;

    uint64_t bytes=0;
    int logical_sector_size=0;
    if (ioctl(fd,BLKGETSIZE64,&bytes) || ioctl(fd,BLKSSZGET,&logical_sector_size))
        return MDI_IO;
    if (logical_sector_size!=512 || !bytes || bytes%512 || bytes/512<2 ||
        bytes>(uint64_t)INT64_MAX)
        return MDI_GEOMETRY;

    out->major_number=maj;
    out->minor_number=min;
    out->disk_sectors=bytes/512;
    out->logical_sector_size=(uint32_t)logical_sector_size;
    return MDI_OK;
}
int mbr_device_identity_matches(const MbrDeviceIdentity *a,
                                const MbrDeviceIdentity *b) {
    return a && b && a->major_number==b->major_number &&
        a->minor_number==b->minor_number && a->disk_sectors==b->disk_sectors &&
        a->logical_sector_size==b->logical_sector_size;
}
MdiResult mbr_device_identity_check_fd(int fd,const MbrDeviceIdentity *expected,
                                       MbrDeviceIdentity *out) {
    if (!expected || !out) return MDI_BAD_ARGUMENT;
    MdiResult r=mbr_device_identity_probe_fd(fd,out);
    if (r!=MDI_OK) return r;
    return mbr_device_identity_matches(out,expected) ? MDI_OK : MDI_IDENTITY_MISMATCH;
}
