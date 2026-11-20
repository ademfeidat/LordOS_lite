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
} MjtJournalRecord;

typedef enum {
    MJTJ_OK = 0,
    MJTJ_INVALID = -1,
    MJTJ_IO = -2,
    MJTJ_EXISTS = -3,
    MJTJ_MISSING = -4,
    MJTJ_CORRUPT = -5,
    MJTJ_STALE = -6,
    MJTJ_UNCERTAIN = -7
} MjtJournalResult;

/* Read a 0600 regular journal file without following symlinks. SHA-256 detects
 * torn/corrupt records; it is not an authentication mechanism. */
MjtJournalResult mjtj_read_at(int dir_fd, const char *name,
                              MjtJournalRecord *out);

/* expected_sequence == 0 creates sequence 1 without replacing an existing file.
 * Updates require an existing valid record with that sequence and require the
 * new record sequence to be expected_sequence + 1. The transaction identity,
 * disk identity, geometry, fingerprint and backup name cannot change.
 *
 * Writes a temporary file, fsyncs it, atomically links/renames it into place,
 * and fsyncs the directory. MJTJ_UNCERTAIN means the final directory fsync
 * failed: callers MUST stop and must not begin the operation being journaled. */
MjtJournalResult mjtj_write_at(int dir_fd, const char *name,
                               const MjtJournalRecord *record,
                               uint64_t expected_sequence);

#endif
