/* mbr_busy.h — conservative read-only MBR disk-use detection. */
#ifndef LORD_MBR_BUSY_H
#define LORD_MBR_BUSY_H
#include "disk_snapshot.h"

/* root="" reads the live system; tests may supply a fake sysfs/proc tree.
 * Returns 1 when in use or when any required kernel state is unavailable,
 * 0 only when the disk and every listed partition are demonstrably free. */
int mbr_partition_busy_at(const char *root, const char *disk, unsigned number);
int mbr_snapshot_busy_at(const char *root, const char *disk,
                         const DiskSnapshot *snapshot);
#endif
