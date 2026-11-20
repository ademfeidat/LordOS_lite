#include "../src/install/mbr_device_identity.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); failures++; } } while (0)

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
    if(failures) { fprintf(stderr,"%d MBR device identity test(s) failed\n",failures); return 1; }
    puts("MBR whole-disk identity tests passed");
    return 0;
}
