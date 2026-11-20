/* mbr_txn_journal.c — atomic, checksummed journal persistence. */
#include "mbr_txn_journal.h"
#include "../pkg/crypto.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <sys/file.h>
#include <unistd.h>

#define J_VERSION 1u
#define J_MAGIC_OFF 0
#define J_VERSION_OFF 8
#define J_LENGTH_OFF 12
#define J_SEQUENCE_OFF 16
#define J_STATE_OFF 24
#define J_MOUNT_OFF 28
#define J_POLICY_OFF 32
#define J_FLAGS_OFF 36
#define J_DEV_OFF 48
#define J_INO_OFF 56
#define J_RDEV_OFF 64
#define J_SECTORS_OFF 72
#define J_SECTOR_SIZE_OFF 80
#define J_FINGERPRINT_OFF 88
#define J_TXID_OFF 152
#define J_BACKUP_OFF 184
#define J_DIGEST_OFF 480

static const uint8_t journal_magic[8] = {'L','M','B','R','T','X','N','1'};

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    uint32_t v=0; for(unsigned i=0;i<4;i++) v|=(uint32_t)p[i]<<(8*i); return v;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v=0; for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v;
}
static int full_read(int fd, uint8_t *p, size_t n) {
    while(n) {
        ssize_t r=read(fd,p,n);
        if(r<0 && errno==EINTR) continue;
        if(r<=0) return -1;
        p+=(size_t)r; n-=(size_t)r;
    }
    return 0;
}
static int full_write(int fd, const uint8_t *p, size_t n) {
    while(n) {
        ssize_t r=write(fd,p,n);
        if(r<0 && errno==EINTR) continue;
        if(r<=0) return -1;
        p+=(size_t)r; n-=(size_t)r;
    }
    return 0;
}
static int valid_name(const char *s) {
    if(!s || !*s || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||
        (*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-')) return 0;
    return 1;
}
static int valid_hex(const char *s, size_t n) {
    if(!s || strnlen(s,n+1)!=n) return 0;
    for(size_t i=0;i<n;i++) if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return 1;
}
static int plan_valid(const MjtPlan *p) {
    if(!p || p->state < MJT_EMPTY || p->state > MJT_RESTORED ||
       p->policy.format_root != 1 || p->policy.format_home > 1 ||
       p->policy.initialize_swap > 1 || p->policy.mount_home > 1 ||
       (p->policy.format_home && !p->policy.mount_home) ||
       (p->mount_mask & ~(MJT_MOUNT_ROOT|MJT_MOUNT_HOME))) return 0;
    const uint8_t flags[] = {p->root_format_started,p->root_format_done,
        p->home_format_started,p->home_format_done,p->swap_started,p->swap_done,
        p->grub_write_started,p->grub_write_verified,p->failure_seen};
    for(size_t i=0;i<sizeof flags;i++) if(flags[i]>1) return 0;
    if(p->root_format_done && !p->root_format_started) return 0;
    if(p->home_format_started && (!p->policy.format_home || !p->root_format_done)) return 0;
    if(p->home_format_done && !p->home_format_started) return 0;
    if(p->swap_started && (!p->policy.initialize_swap || !p->root_format_done)) return 0;
    if(p->swap_done && !p->swap_started) return 0;
    if(p->grub_write_started && (!p->root_format_done || p->state < MJT_GRUB_WRITE_STARTED)) return 0;
    if(p->grub_write_verified && !p->grub_write_started) return 0;
    return 1;
}
static int record_valid(const MjtJournalRecord *r) {
    return r && plan_valid(&r->plan) && r->plan.sequence > 0 &&
        r->st_rdev != 0 && r->disk_sectors > 0 &&
        (r->logical_sector_size==512 || r->logical_sector_size==1024 ||
         r->logical_sector_size==2048 || r->logical_sector_size==4096) &&
        valid_hex(r->snapshot_sha256,64) && valid_hex(r->transaction_id,32) &&
        strnlen(r->backup_name,MJTJ_NAME_MAX+1)>0 &&
        strnlen(r->backup_name,MJTJ_NAME_MAX+1)<=MJTJ_NAME_MAX &&
        valid_name(r->backup_name);
}
static void encode(const MjtJournalRecord *r, uint8_t b[MJTJ_RECORD_SIZE]) {
    memset(b,0,MJTJ_RECORD_SIZE);
    memcpy(b+J_MAGIC_OFF,journal_magic,8);
    put32(b+J_VERSION_OFF,J_VERSION); put32(b+J_LENGTH_OFF,MJTJ_RECORD_SIZE);
    put64(b+J_SEQUENCE_OFF,r->plan.sequence);
    put32(b+J_STATE_OFF,(uint32_t)r->plan.state);
    put32(b+J_MOUNT_OFF,r->plan.mount_mask);
    b[J_POLICY_OFF]=r->plan.policy.format_root;
    b[J_POLICY_OFF+1]=r->plan.policy.format_home;
    b[J_POLICY_OFF+2]=r->plan.policy.initialize_swap;
    b[J_POLICY_OFF+3]=r->plan.policy.mount_home;
    const uint8_t flags[]={r->plan.root_format_started,r->plan.root_format_done,
        r->plan.home_format_started,r->plan.home_format_done,r->plan.swap_started,
        r->plan.swap_done,r->plan.grub_write_started,r->plan.grub_write_verified,
        r->plan.failure_seen};
    memcpy(b+J_FLAGS_OFF,flags,sizeof flags);
    put64(b+J_DEV_OFF,r->st_dev); put64(b+J_INO_OFF,r->st_ino);
    put64(b+J_RDEV_OFF,r->st_rdev); put64(b+J_SECTORS_OFF,r->disk_sectors);
    put32(b+J_SECTOR_SIZE_OFF,r->logical_sector_size);
    memcpy(b+J_FINGERPRINT_OFF,r->snapshot_sha256,64);
    memcpy(b+J_TXID_OFF,r->transaction_id,32);
    memcpy(b+J_BACKUP_OFF,r->backup_name,strnlen(r->backup_name,MJTJ_NAME_MAX));
    sha256(b,J_DIGEST_OFF,b+J_DIGEST_OFF);
}
static int decode(const uint8_t b[MJTJ_RECORD_SIZE], MjtJournalRecord *r) {
    if(memcmp(b+J_MAGIC_OFF,journal_magic,8) || get32(b+J_VERSION_OFF)!=J_VERSION ||
       get32(b+J_LENGTH_OFF)!=MJTJ_RECORD_SIZE) return -1;
    uint8_t copy[MJTJ_RECORD_SIZE], digest[32];
    memcpy(copy,b,sizeof copy); memset(copy+J_DIGEST_OFF,0,32);
    sha256(copy,J_DIGEST_OFF,digest);
    if(memcmp(digest,b+J_DIGEST_OFF,32)) return -1;
    memset(r,0,sizeof *r);
    r->plan.sequence=get64(b+J_SEQUENCE_OFF);
    r->plan.state=(MjtState)get32(b+J_STATE_OFF);
    r->plan.mount_mask=get32(b+J_MOUNT_OFF);
    r->plan.policy.format_root=b[J_POLICY_OFF];
    r->plan.policy.format_home=b[J_POLICY_OFF+1];
    r->plan.policy.initialize_swap=b[J_POLICY_OFF+2];
    r->plan.policy.mount_home=b[J_POLICY_OFF+3];
    r->plan.root_format_started=b[J_FLAGS_OFF];
    r->plan.root_format_done=b[J_FLAGS_OFF+1];
    r->plan.home_format_started=b[J_FLAGS_OFF+2];
    r->plan.home_format_done=b[J_FLAGS_OFF+3];
    r->plan.swap_started=b[J_FLAGS_OFF+4];
    r->plan.swap_done=b[J_FLAGS_OFF+5];
    r->plan.grub_write_started=b[J_FLAGS_OFF+6];
    r->plan.grub_write_verified=b[J_FLAGS_OFF+7];
    r->plan.failure_seen=b[J_FLAGS_OFF+8];
    r->st_dev=get64(b+J_DEV_OFF); r->st_ino=get64(b+J_INO_OFF);
    r->st_rdev=get64(b+J_RDEV_OFF); r->disk_sectors=get64(b+J_SECTORS_OFF);
    r->logical_sector_size=get32(b+J_SECTOR_SIZE_OFF);
    memcpy(r->snapshot_sha256,b+J_FINGERPRINT_OFF,64); r->snapshot_sha256[64]=0;
    memcpy(r->transaction_id,b+J_TXID_OFF,32); r->transaction_id[32]=0;
    memcpy(r->backup_name,b+J_BACKUP_OFF,MJTJ_NAME_MAX); r->backup_name[MJTJ_NAME_MAX]=0;
    return record_valid(r) ? 0 : -1;
}
static MjtJournalResult read_fd(int fd, MjtJournalRecord *out) {
    struct stat st;
    uint8_t b[MJTJ_RECORD_SIZE];
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1 ||
       (st.st_mode&0777)!=0600 || st.st_size!=MJTJ_RECORD_SIZE) return MJTJ_CORRUPT;
    if(lseek(fd,0,SEEK_SET)<0 || full_read(fd,b,sizeof b)) return MJTJ_IO;
    return decode(b,out)==0 ? MJTJ_OK : MJTJ_CORRUPT;
}
static int trusted_journal_dir(int dir_fd,struct stat *out) {
    struct stat ds;
    if(fstat(dir_fd,&ds) || !S_ISDIR(ds.st_mode) ||
       ds.st_uid!=geteuid() || (ds.st_mode&0022)!=0) return 0;
    if(out) *out=ds;
    return 1;
}
MjtJournalResult mjtj_lock_at(int dir_fd,const char *name,MjtJournalLock *out) {
    struct stat ds,ls;
    if(dir_fd<0 || !out || !valid_name(name) || !trusted_journal_dir(dir_fd,&ds))
        return MJTJ_INVALID;
    out->fd=-1; out->dir_dev=0; out->dir_ino=0; out->held=0;
    int fd=openat(dir_fd,name,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0) return errno==ELOOP ? MJTJ_CORRUPT : MJTJ_IO;
    if(fstat(fd,&ls) || !S_ISREG(ls.st_mode) || ls.st_nlink!=1 ||
       ls.st_uid!=geteuid() || fchmod(fd,0600)) {
        close(fd); return MJTJ_CORRUPT;
    }
    int rc;
    do { rc=flock(fd,LOCK_EX|LOCK_NB); } while(rc<0 && errno==EINTR);
    if(rc<0) {
        int e=errno; close(fd);
        return (e==EWOULDBLOCK || e==EAGAIN) ? MJTJ_BUSY : MJTJ_IO;
    }
    out->fd=fd; out->dir_dev=(uint64_t)ds.st_dev; out->dir_ino=(uint64_t)ds.st_ino; out->held=1;
    return MJTJ_OK;
}
void mjtj_unlock(MjtJournalLock *lock) {
    if(!lock) return;
    if(lock->fd>=0) {
        (void)flock(lock->fd,LOCK_UN);
        (void)close(lock->fd);
    }
    lock->fd=-1; lock->dir_dev=0; lock->dir_ino=0; lock->held=0;
}
static int lock_valid(int dir_fd,const MjtJournalLock *lock) {
    struct stat ds,ls;
    if(!lock || !lock->held || lock->fd<0 || !trusted_journal_dir(dir_fd,&ds) ||
       (uint64_t)ds.st_dev!=lock->dir_dev ||
       (uint64_t)ds.st_ino!=lock->dir_ino || fstat(lock->fd,&ls) ||
       !S_ISREG(ls.st_mode) || ls.st_nlink!=1 || ls.st_uid!=geteuid() ||
       (ls.st_mode&0777)!=0600)
        return 0;
    int rc;
    do { rc=flock(lock->fd,LOCK_EX|LOCK_NB); } while(rc<0 && errno==EINTR);
    return rc==0;
}
MjtJournalResult mjtj_read_at(int dir_fd,const char *name,MjtJournalRecord *out) {
    struct stat ds;
    if(dir_fd<0 || !valid_name(name) || !out || fstat(dir_fd,&ds) || !S_ISDIR(ds.st_mode))
        return MJTJ_INVALID;
    int fd=openat(dir_fd,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) return errno==ENOENT ? MJTJ_MISSING : (errno==ELOOP ? MJTJ_CORRUPT : MJTJ_IO);
    MjtJournalResult r=read_fd(fd,out);
    if(close(fd) && r==MJTJ_OK) r=MJTJ_IO;
    return r;
}
static int same_identity(const MjtJournalRecord *a,const MjtJournalRecord *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino &&
        a->st_rdev==b->st_rdev && a->disk_sectors==b->disk_sectors &&
        a->logical_sector_size==b->logical_sector_size &&
        a->plan.policy.format_root==b->plan.policy.format_root &&
        a->plan.policy.format_home==b->plan.policy.format_home &&
        a->plan.policy.initialize_swap==b->plan.policy.initialize_swap &&
        a->plan.policy.mount_home==b->plan.policy.mount_home &&
        !strcmp(a->snapshot_sha256,b->snapshot_sha256) &&
        !strcmp(a->transaction_id,b->transaction_id) &&
        !strcmp(a->backup_name,b->backup_name);
}
static int plan_equal(const MjtPlan *a,const MjtPlan *b) {
    return a->state==b->state && a->sequence==b->sequence &&
        a->mount_mask==b->mount_mask &&
        a->policy.format_root==b->policy.format_root &&
        a->policy.format_home==b->policy.format_home &&
        a->policy.initialize_swap==b->policy.initialize_swap &&
        a->policy.mount_home==b->policy.mount_home &&
        a->root_format_started==b->root_format_started &&
        a->root_format_done==b->root_format_done &&
        a->home_format_started==b->home_format_started &&
        a->home_format_done==b->home_format_done &&
        a->swap_started==b->swap_started && a->swap_done==b->swap_done &&
        a->grub_write_started==b->grub_write_started &&
        a->grub_write_verified==b->grub_write_verified &&
        a->failure_seen==b->failure_seen;
}
static int one_valid_transition(const MjtPlan *old,const MjtPlan *next) {
    for(int ev=MJT_EV_PREFLIGHT_OK;ev<=MJT_EV_RESTORE_VERIFIED;ev++) {
        MjtPlan candidate=*old;
        if(mjt_apply(&candidate,(MjtEvent)ev)==MJT_OK && plan_equal(&candidate,next))
            return 1;
    }
    return 0;
}
MjtJournalResult mjtj_write_at(int dir_fd,const MjtJournalLock *lock,
                               const char *name,const MjtJournalRecord *r,uint64_t expected) {
    if(dir_fd<0 || !valid_name(name) || !record_valid(r) ||
       expected==UINT64_MAX || !lock_valid(dir_fd,lock) ||
       (expected != 0 && r->plan.sequence != expected+1)) return MJTJ_INVALID;
    MjtJournalRecord old;
    MjtJournalResult rr=mjtj_read_at(dir_fd,name,&old);
    if(expected==0) {
        if(rr==MJTJ_OK) return MJTJ_EXISTS;
        if(rr!=MJTJ_MISSING) return rr;
        if(r->plan.sequence!=3 || r->plan.state!=MJT_JOURNAL_DURABLE ||
           r->plan.mount_mask || r->plan.root_format_started ||
           r->plan.home_format_started || r->plan.swap_started ||
           r->plan.grub_write_started) return MJTJ_INVALID;
    } else {
        if(rr!=MJTJ_OK) return rr;
        if(old.plan.sequence!=expected || !same_identity(&old,r) ||
           !one_valid_transition(&old.plan,&r->plan)) return MJTJ_STALE;
    }
    uint64_t nonce;
    if(getrandom(&nonce,sizeof nonce,0)!=(ssize_t)sizeof nonce) return MJTJ_IO;
    char temp[128];
    int n=snprintf(temp,sizeof temp,".%s.tmp.%ld.%016llx",name,(long)getpid(),
                   (unsigned long long)nonce);
    if(n<0 || (size_t)n>=sizeof temp) return MJTJ_INVALID;
    int fd=openat(dir_fd,temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0) return MJTJ_IO;
    uint8_t b[MJTJ_RECORD_SIZE]; encode(r,b);
    int failed=fchmod(fd,0600) || full_write(fd,b,sizeof b) || fsync(fd);
    if(close(fd)) failed=1;
    if(failed) { unlinkat(dir_fd,temp,0); return MJTJ_IO; }
    if(expected==0) {
        if(linkat(dir_fd,temp,dir_fd,name,0)) {
            int e=errno; unlinkat(dir_fd,temp,0);
            return e==EEXIST ? MJTJ_EXISTS : MJTJ_IO;
        }
        if(unlinkat(dir_fd,temp,0)) {
            (void)unlinkat(dir_fd,name,0);
            (void)unlinkat(dir_fd,temp,0);
            (void)fsync(dir_fd);
            return MJTJ_UNCERTAIN;
        }
    } else {
        if(renameat(dir_fd,temp,dir_fd,name)) {
            unlinkat(dir_fd,temp,0); return MJTJ_IO;
        }
    }
    if(fsync(dir_fd)) return MJTJ_UNCERTAIN;
    return MJTJ_OK;
}
