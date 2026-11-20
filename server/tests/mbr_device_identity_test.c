#include "../src/install/mbr_device_identity.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); failures++; } } while (0)

static void make_dir(const char *p) { CHECK(mkdir(p,0700)==0 || errno==EEXIST); }
static void write_text(const char *p,const char *s) {
    FILE *f=fopen(p,"w"); CHECK(f!=NULL); if(f) { CHECK(fputs(s,f)>=0); CHECK(fclose(f)==0); }
}
int main(void) {
    MbrDeviceIdentity a={.major_number=8,.minor_number=0,.disk_sectors=1000000,.logical_sector_size=512,.kernel_name="sda"};
    MbrDeviceIdentity b=a;
    CHECK(mbr_device_identity_matches(&a,&b));
    b.minor_number++;
    CHECK(!mbr_device_identity_matches(&a,&b));
    b=a; b.major_number++;
    CHECK(!mbr_device_identity_matches(&a,&b));
    b=a; b.disk_sectors++;
    CHECK(!mbr_device_identity_matches(&a,&b));
    b=a; b.logical_sector_size=4096;
    CHECK(!mbr_device_identity_matches(&a,&b));
    b=a; strcpy(b.kernel_name,"sdb");
    CHECK(!mbr_device_identity_matches(&a,&b));
    CHECK(!mbr_device_identity_matches(NULL,&a));

    char path[]="/tmp/lordos-mbr-device-XXXXXX";
    int w=mkstemp(path);
    CHECK(w>=0);
    if(w>=0) {
        CHECK(mbr_device_identity_probe_fd(w,&b)==MDI_NOT_READ_ONLY);
        close(w);
        int ro=open(path,O_RDONLY|O_CLOEXEC);
        CHECK(ro>=0);
        if(ro>=0) {
            CHECK(mbr_device_identity_probe_fd(ro,&b)==MDI_NOT_BLOCK_DEVICE);
            CHECK(mbr_device_identity_check_fd(ro,&a,&b)==MDI_NOT_BLOCK_DEVICE);
            close(ro);
        }
        unlink(path);
    }

    int nullfd=open("/dev/null",O_RDONLY|O_CLOEXEC);
    CHECK(nullfd>=0);
    if(nullfd>=0) {
        CHECK(mbr_device_identity_probe_fd(nullfd,&b)==MDI_NOT_BLOCK_DEVICE);
        close(nullfd);
    }
    CHECK(mbr_device_identity_probe_fd(-1,&b)==MDI_BAD_ARGUMENT);
    CHECK(mbr_device_identity_probe_fd(0,NULL)==MDI_BAD_ARGUMENT);

    /* Fake sysfs proves the partition number is a child of the selected disk
     * and that its current major/minor is read from that exact child. */
    char root[]="/tmp/lordos-mbr-sysfs-XXXXXX";
    char *tree=mkdtemp(root); CHECK(tree!=NULL);
    if(tree) {
        char p[512];
        snprintf(p,sizeof p,"%s/sys",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/sys/class",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/sys/class/block",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/dev",tree); make_dir(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/dev",tree); write_text(p,"8:0\n");
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1/partition",tree); write_text(p,"1\n");
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1/dev",tree); write_text(p,"8:1\n");
        snprintf(p,sizeof p,"%s/dev/sda1",tree); write_text(p,"not-a-device\n");
        uint32_t pma=0,pmi=0;
        CHECK(mbr_partition_sysfs_identity_at(tree,&a,1,&pma,&pmi)==MDI_OK);
        CHECK(pma==8 && pmi==1);
        CHECK(mbr_partition_device_check_at(tree,&a,1,&pma,&pmi)==MDI_NOT_BLOCK_DEVICE);
        MbrDeviceIdentity wrong_disk=a; wrong_disk.major_number=9;
        CHECK(mbr_partition_sysfs_identity_at(tree,&wrong_disk,1,&pma,&pmi)==MDI_IDENTITY_MISMATCH);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1/partition",tree); write_text(p,"2\n");
        CHECK(mbr_partition_sysfs_identity_at(tree,&a,1,&pma,&pmi)==MDI_IDENTITY_MISMATCH);
        snprintf(p,sizeof p,"%s/dev/sda1",tree); unlink(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1/dev",tree); unlink(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1/partition",tree); unlink(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/sda1",tree); rmdir(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda/dev",tree); unlink(p);
        snprintf(p,sizeof p,"%s/sys/class/block/sda",tree); rmdir(p);
        snprintf(p,sizeof p,"%s/sys/class/block",tree); rmdir(p);
        snprintf(p,sizeof p,"%s/sys/class",tree); rmdir(p);
        snprintf(p,sizeof p,"%s/sys",tree); rmdir(p);
        snprintf(p,sizeof p,"%s/dev",tree); rmdir(p);
        rmdir(tree);
    }
    if(failures) { fprintf(stderr,"%d MBR device identity test(s) failed\n",failures); return 1; }
    puts("MBR whole-disk identity tests passed");
    return 0;
}
