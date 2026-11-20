#include "../src/install/mbr_txn_journal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int fail_fsync_at;
static int fsync_calls;
extern int __real_fsync(int fd);
int __wrap_fsync(int fd) {
    fsync_calls++;
    if (fail_fsync_at && fsync_calls == fail_fsync_at) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); failures++; } } while (0)

static void fill_record(MjtJournalRecord *r) {
    memset(r,0,sizeof *r);
    MjtPolicy p={1,0,0,0};
    CHECK(mjt_init(&r->plan,&p)==MJT_OK);
    CHECK(mjt_apply(&r->plan,MJT_EV_PREFLIGHT_OK)==MJT_OK);
    CHECK(mjt_apply(&r->plan,MJT_EV_BACKUP_DURABLE)==MJT_OK);
    CHECK(mjt_apply(&r->plan,MJT_EV_JOURNAL_DURABLE)==MJT_OK);
    r->st_dev=11; r->st_ino=0; r->st_rdev=0x1234;
    r->disk_sectors=8192; r->logical_sector_size=512;
    memset(r->snapshot_sha256,'a',64); r->snapshot_sha256[64]=0;
    memset(r->transaction_id,'b',32); r->transaction_id[32]=0;
    strcpy(r->backup_name,"grub.backup");
}
int main(void) {
    char tmp[]="/tmp/lordos-mbr-journal-XXXXXX";
    char *dir=mkdtemp(tmp);
    CHECK(dir!=NULL);
    if(!dir) return 1;
    int dfd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    CHECK(dfd>=0);
    if(dfd<0) return 1;

    MjtJournalRecord r, got;
    fill_record(&r);
    CHECK(r.plan.sequence==3);
    CHECK(mjtj_write_at(dfd,"install.txn",&r,0)==MJTJ_OK);
    CHECK(mjtj_read_at(dfd,"install.txn",&got)==MJTJ_OK);
    CHECK(got.plan.sequence==3 && got.plan.state==MJT_JOURNAL_DURABLE);
    CHECK(got.st_rdev==r.st_rdev && !strcmp(got.snapshot_sha256,r.snapshot_sha256));
    CHECK(mjtj_write_at(dfd,"install.txn",&r,0)==MJTJ_EXISTS);

    CHECK(mjt_apply(&r.plan,MJT_EV_ROOT_FORMAT_BEGIN)==MJT_OK);
    CHECK(r.plan.sequence==4);
    CHECK(mjtj_write_at(dfd,"install.txn",&r,3)==MJTJ_OK);
    CHECK(mjtj_read_at(dfd,"install.txn",&got)==MJTJ_OK);
    CHECK(got.plan.sequence==4 && got.plan.root_format_started);

    MjtJournalRecord stale=r;
    stale.plan.sequence=5;
    stale.st_rdev++;
    CHECK(mjtj_write_at(dfd,"install.txn",&stale,4)==MJTJ_STALE);
    CHECK(mjtj_read_at(dfd,"install.txn",&got)==MJTJ_OK && got.plan.sequence==4);

    CHECK(symlinkat("install.txn",dfd,"journal-link")==0);
    CHECK(mjtj_read_at(dfd,"journal-link",&got)==MJTJ_CORRUPT);
    CHECK(unlinkat(dfd,"journal-link",0)==0);

    int jf=openat(dfd,"install.txn",O_RDWR|O_CLOEXEC);
    CHECK(jf>=0);
    if(jf>=0) {
        unsigned char bad=0xff;
        CHECK(pwrite(jf,&bad,1,100)==1);
        close(jf);
    }
    CHECK(mjtj_read_at(dfd,"install.txn",&got)==MJTJ_CORRUPT);
    CHECK(unlinkat(dfd,"install.txn",0)==0);

    /* Failure syncing the temporary file: no final journal may appear. */
    fill_record(&r);
    fsync_calls=0; fail_fsync_at=1;
    CHECK(mjtj_write_at(dfd,"file-sync.txn",&r,0)==MJTJ_IO);
    fail_fsync_at=0;
    CHECK(faccessat(dfd,"file-sync.txn",F_OK,0)!=0);

    /* Failure syncing the containing directory is explicitly uncertain. The
     * operation being journaled must not run, even if the file is readable. */
    fsync_calls=0; fail_fsync_at=2;
    CHECK(mjtj_write_at(dfd,"dir-sync.txn",&r,0)==MJTJ_UNCERTAIN);
    fail_fsync_at=0;
    CHECK(mjtj_read_at(dfd,"dir-sync.txn",&got)==MJTJ_OK);
    CHECK(got.plan.sequence==3);

    unlinkat(dfd,"dir-sync.txn",0);
    close(dfd);
    rmdir(dir);
    if(failures) { fprintf(stderr,"%d MBR journal test(s) failed\n",failures); return 1; }
    puts("MBR transaction journal tests passed");
    return 0;
}
