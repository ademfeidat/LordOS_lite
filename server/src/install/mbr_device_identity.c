/* mbr_device_identity.c — read-only whole-disk block-device identity check. */
#include "mbr_device_identity.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <string.h>
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
    char sysfs_target[PATH_MAX];
    ssize_t target_len=readlink(sysfs_path,sysfs_target,sizeof sysfs_target-1);
    if(target_len<0 || (size_t)target_len>=sizeof sysfs_target-1) return MDI_IO;
    sysfs_target[target_len]=0;
    if(strstr(sysfs_target,"/virtual/block/")) return MDI_NOT_WHOLE_DISK;
    const char *leaf=strrchr(sysfs_target,'/');
    leaf=leaf ? leaf+1 : sysfs_target;
    size_t leaf_len=strnlen(leaf,sizeof out->kernel_name);
    if(!leaf_len || leaf_len>=sizeof out->kernel_name) return MDI_IO;
    for(size_t i=0;i<leaf_len;i++)
        if(!((leaf[i]>='a'&&leaf[i]<='z')||(leaf[i]>='A'&&leaf[i]<='Z')||
             (leaf[i]>='0'&&leaf[i]<='9')||leaf[i]=='_'||leaf[i]=='-'))
            return MDI_NOT_WHOLE_DISK;
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
    memcpy(out->kernel_name,leaf,leaf_len);
    out->kernel_name[leaf_len]=0;
    return MDI_OK;
}
int mbr_device_identity_matches(const MbrDeviceIdentity *a,
                                const MbrDeviceIdentity *b) {
    return a && b && a->major_number==b->major_number &&
        a->minor_number==b->minor_number && a->disk_sectors==b->disk_sectors &&
        a->logical_sector_size==b->logical_sector_size &&
        strnlen(a->kernel_name,sizeof a->kernel_name)<sizeof a->kernel_name &&
        strnlen(b->kernel_name,sizeof b->kernel_name)<sizeof b->kernel_name &&
        a->kernel_name[0] && !strcmp(a->kernel_name,b->kernel_name);
}
MdiResult mbr_device_identity_check_fd(int fd,const MbrDeviceIdentity *expected,
                                       MbrDeviceIdentity *out) {
    if (!expected || !out) return MDI_BAD_ARGUMENT;
    MdiResult r=mbr_device_identity_probe_fd(fd,out);
    if (r!=MDI_OK) return r;
    return mbr_device_identity_matches(out,expected) ? MDI_OK : MDI_IDENTITY_MISMATCH;
}

static int valid_kernel_name(const char *s) {
    if(!s || !*s || strnlen(s,64)>=64) return 0;
    for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||
        (*s>='0'&&*s<='9')||*s=='_'||*s=='-')) return 0;
    return 1;
}
static int partition_kernel_name(const char *disk,unsigned number,char *out,size_t n) {
    if(!valid_kernel_name(disk) || !number || number>255 || !out || !n) return -1;
    size_t len=strlen(disk);
    int k=snprintf(out,n,"%s%s%u",disk,
        (disk[len-1]>='0'&&disk[len-1]<='9') ? "p" : "",number);
    return k<0 || (size_t)k>=n ? -1 : 0;
}
static int read_pair(const char *path,uint32_t *a,uint32_t *b) {
    FILE *f=fopen(path,"r");
    if(!f) return -1;
    unsigned x,y; char extra;
    int n=fscanf(f,"%u:%u %c",&x,&y,&extra);
    int bad=ferror(f) || n!=2;
    if(fclose(f)) bad=1;
    if(bad) return -1;
    *a=x; *b=y; return 0;
}
static int read_number(const char *path,unsigned *value) {
    FILE *f=fopen(path,"r");
    if(!f) return -1;
    unsigned x; char extra;
    int n=fscanf(f,"%u %c",&x,&extra);
    int bad=ferror(f) || n!=1;
    if(fclose(f)) bad=1;
    if(bad) return -1;
    *value=x; return 0;
}
MdiResult mbr_partition_sysfs_identity_at(const char *root,
                                          const MbrDeviceIdentity *disk,
                                          unsigned number,
                                          uint32_t *part_major,
                                          uint32_t *part_minor) {
    if(!disk || !valid_kernel_name(disk->kernel_name) || !number ||
       number>255 || !part_major || !part_minor) return MDI_BAD_ARGUMENT;
    const char *base=root ? root : "";
    char disk_dev[PATH_MAX], part_dir[PATH_MAX], part_num[PATH_MAX], part_dev[PATH_MAX];
    int n=snprintf(disk_dev,sizeof disk_dev,"%s/sys/class/block/%s/dev",base,disk->kernel_name);
    if(n<0 || (size_t)n>=sizeof disk_dev) return MDI_IO;
    uint32_t disk_major,disk_minor;
    if(read_pair(disk_dev,&disk_major,&disk_minor) ||
       disk_major!=disk->major_number || disk_minor!=disk->minor_number)
        return MDI_IDENTITY_MISMATCH;
    char part_name[96];
    if(partition_kernel_name(disk->kernel_name,number,part_name,sizeof part_name))
        return MDI_BAD_ARGUMENT;
    n=snprintf(part_dir,sizeof part_dir,"%s/sys/class/block/%s/%s",base,disk->kernel_name,part_name);
    if(n<0 || (size_t)n>=sizeof part_dir) return MDI_IO;
    struct stat st;
    if(stat(part_dir,&st) || !S_ISDIR(st.st_mode)) return MDI_IO;
    n=snprintf(part_num,sizeof part_num,"%s/partition",part_dir);
    if(n<0 || (size_t)n>=sizeof part_num) return MDI_IO;
    unsigned observed_number;
    if(read_number(part_num,&observed_number) || observed_number!=number)
        return MDI_IDENTITY_MISMATCH;
    n=snprintf(part_dev,sizeof part_dev,"%s/dev",part_dir);
    if(n<0 || (size_t)n>=sizeof part_dev) return MDI_IO;
    if(read_pair(part_dev,part_major,part_minor)) return MDI_IO;
    if(*part_major==disk->major_number && *part_minor==disk->minor_number)
        return MDI_IDENTITY_MISMATCH;
    return MDI_OK;
}
MdiResult mbr_partition_device_check_at(const char *root,
                                        const MbrDeviceIdentity *disk,
                                        unsigned number,
                                        uint32_t *part_major,
                                        uint32_t *part_minor) {
    uint32_t ma,mi;
    MdiResult r=mbr_partition_sysfs_identity_at(root,disk,number,&ma,&mi);
    if(r!=MDI_OK) return r;
    char part_name[96], node_path[PATH_MAX];
    if(partition_kernel_name(disk->kernel_name,number,part_name,sizeof part_name))
        return MDI_BAD_ARGUMENT;
    int n=snprintf(node_path,sizeof node_path,"%s/dev/%s",root ? root : "",part_name);
    if(n<0 || (size_t)n>=sizeof node_path) return MDI_IO;
    struct stat st;
    if(lstat(node_path,&st)) return MDI_IO;
    if(!S_ISBLK(st.st_mode)) return MDI_NOT_BLOCK_DEVICE;
    if((uint32_t)major(st.st_rdev)!=ma || (uint32_t)minor(st.st_rdev)!=mi)
        return MDI_IDENTITY_MISMATCH;
    if(part_major) *part_major=ma;
    if(part_minor) *part_minor=mi;
    return MDI_OK;
}
