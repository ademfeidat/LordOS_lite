/* mbr_txn_journal.h — durable atomic journal records for the MBR transaction.
 * The caller must hold an exclusive per-disk installer lock and must not perform
 * an operation until its corresponding journal update returns MJTJ_OK. */
#ifndef LORD_INSTALL_MBR_TXN_JOURNAL_H
#define LORD_INSTALL_MBR_TXN_JOURNAL_H

#include "mbr_txn_plan.h"
#include <stdint.h>

#define MJTJ_RECORD_SIZE 512
#define MJTJ_NAME_MAX 63

typedef struct {
    MjtPlan plan;
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_rdev;
    uint64_t disk_sectors;
    uint32_t logical_sector_size;
    char snapshot_sha256[65];
    char transaction_id[33];
    char backup_name[MJTJ_NAME_MAX + 1];
    /* Exact selected MBR partition identities, captured before any mutation. */
    uint32_t root_part, home_part, swap_part, swap_file_mib;
    uint32_t root_major, root_minor, home_major, home_minor, swap_major, swap_minor;
    char backup_sha256[65]; /* digest of the complete GRUB backup file */
    /* Immutable identity of the audited GRUB artifact set used for this transaction. */
    uint64_t grub_write_set_bytes;
    char grub_manifest_sha256[65];
} MjtJournalRecord;

typedef struct {
    int fd;
    uint64_t dir_dev;
    uint64_t dir_ino;
    uint8_t held;
} MjtJournalLock;

typedef enum {
    MJTJ_OK = 0,
    MJTJ_INVALID = -1,
    MJTJ_IO = -2,
    MJTJ_EXISTS = -3,
    MJTJ_MISSING = -4,
    MJTJ_CORRUPT = -5,
    MJTJ_STALE = -6,
    MJTJ_UNCERTAIN = -7,
    MJTJ_BUSY = -8
} MjtJournalResult;

/* Acquire an exclusive per-transaction lock in the trusted journal directory.
 * Keep it open for the entire installer transaction, not just one journal write. */
MjtJournalResult mjtj_lock_at(int dir_fd, const char *lock_name,
                              MjtJournalLock *out);
void mjtj_unlock(MjtJournalLock *lock);

/* Read a 0600 regular journal file without following symlinks. SHA-256 detects
 * torn/corrupt records; it is not an authentication mechanism. */
MjtJournalResult mjtj_read_at(int dir_fd, const char *name,
                              MjtJournalRecord *out);

/* expected_sequence == 0 creates the initial durable JOURNAL_DURABLE record (sequence 3) without replacing an existing file.
 * Updates require an existing valid record with that sequence and require the
 * new record sequence to be expected_sequence + 1. The transaction identity,
 * disk identity, geometry, fingerprint, selected partition identities and
 * complete GRUB backup-file SHA-256, audited GRUB manifest SHA-256 and write-set length cannot change.
 *
 * Writes a temporary file, fsyncs it, atomically links/renames it into place,
 * and fsyncs the directory. MJTJ_UNCERTAIN means the final directory fsync
 * failed: callers MUST stop and must not begin the operation being journaled. */
MjtJournalResult mjtj_write_at(int dir_fd, const MjtJournalLock *lock,
                               const char *name, const MjtJournalRecord *record,
                               uint64_t expected_sequence);

#endif
