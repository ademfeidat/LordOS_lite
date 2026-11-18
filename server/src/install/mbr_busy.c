/* mbr_busy.c — conservative read-only block-device use checks. */
#include "mbr_busy.h"

#include <dirent.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int path_join(char *out, size_t n, const char *root, const char *rel) {
    int k = snprintf(out, n, "%s%s", root ? root : "", rel);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

/* Return 1 for a complete line, 0 at clean EOF, and -1 on I/O or truncation. */
static int read_checked_line(FILE *f, char *line, size_t n) {
    if (!fgets(line, (int)n, f)) return ferror(f) ? -1 : 0;
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') return 1;
    if (feof(f)) return 1;
    return -1;
}

static int read_text(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, (int)n, f)) { fclose(f); return -1; }
    fclose(f);
    out[strcspn(out, "\r\n ")] = 0;
    return out[0] ? 0 : -1;
}

static int mount_source_is(const char *line, const char *name) {
    const char *dash = strstr(line, " - ");
    if (!dash) return 0;
    char source[PATH_MAX];
    if (sscanf(dash + 3, "%*s %4095s", source) != 1 ||
        strncmp(source, "/dev/", 5)) return 0;
    char *out = source;
    for (char *p = source; *p; p++) {
        if (p[0] == '\\' && p[1] >= '0' && p[1] <= '3' &&
            p[2] >= '0' && p[2] <= '7' && p[3] >= '0' && p[3] <= '7') {
            *out++ = (char)((p[1]-'0')*64 + (p[2]-'0')*8 + (p[3]-'0'));
            p += 3;
        } else *out++ = *p;
    }
    *out = 0;
    const char *leaf = strrchr(source, '/');
    return leaf && !strcmp(leaf + 1, name);
}

/* Btrfs can mount several devices behind one anonymous mount device.
 * Until sysfs member enumeration is verified, any active btrfs mount blocks
 * destructive use of every candidate disk (conservative false positives).
 */
static int mount_is_btrfs(const char *line) {
    const char *dash = strstr(line, " - ");
    char fstype[32];
    return dash && sscanf(dash + 3, "%31s", fstype) == 1 &&
           !strcmp(fstype, "btrfs");
}

static int block_busy(const char *root, const char *name) {
    char path[PATH_MAX], value[64];
    unsigned ma, mi;
    int k = snprintf(path, sizeof path, "%s/sys/class/block/%s/dev",
                     root ? root : "", name);
    if (k < 0 || (size_t)k >= sizeof path ||
        read_text(path, value, sizeof value) ||
        sscanf(value, "%u:%u", &ma, &mi) != 2) return 1;

    if (path_join(path, sizeof path, root, "/proc/self/mountinfo")) return 1;
    FILE *mounts = fopen(path, "r");
    if (!mounts) return 1;
    char line[PATH_MAX * 2];
    int busy = 0, line_status;
    while ((line_status = read_checked_line(mounts, line, sizeof line)) > 0) {
        unsigned a, b;
        if (sscanf(line, "%*u %*u %u:%u", &a, &b) != 2) {
            busy = 1; break;
        }
        if ((a == ma && b == mi) ||
            (a == 0 && mount_source_is(line, name)) ||
            mount_is_btrfs(line)) {
            busy = 1; break;
        }
    }
    if (line_status < 0) busy = 1;
    fclose(mounts);
    if (busy) return 1;

    if (path_join(path, sizeof path, root, "/proc/swaps")) return 1;
    FILE *swaps = fopen(path, "r");
    if (!swaps) return 1;
    line_status = read_checked_line(swaps, line, sizeof line);
    if (line_status != 1) busy = 1; /* Missing or truncated header. */
    while (!busy &&
           (line_status = read_checked_line(swaps, line, sizeof line)) > 0) {
        char source[PATH_MAX];
        if (sscanf(line, "%4095s", source) != 1) { busy = 1; break; }
        const char *leaf = strrchr(source, '/');
        leaf = leaf ? leaf + 1 : source;
        if (!strcmp(leaf, name)) { busy = 1; break; }
        struct stat st;
        if (stat(source, &st)) { busy = 1; break; }
        if (S_ISBLK(st.st_mode) &&
            major(st.st_rdev) == ma && minor(st.st_rdev) == mi) {
            busy = 1; break;
        }
    }
    if (line_status < 0) busy = 1;
    fclose(swaps);
    if (busy) return 1;

    k = snprintf(path, sizeof path, "%s/sys/class/block/%s/holders",
                 root ? root : "", name);
    if (k < 0 || (size_t)k >= sizeof path) return 1;
    DIR *holders = opendir(path);
    if (!holders) return 1;
    struct dirent *entry;
    while ((entry = readdir(holders)))
        if (entry->d_name[0] != '.') { busy = 1; break; }
    closedir(holders);
    return busy;
}

int mbr_partition_busy_at(const char *root, const char *disk, unsigned number) {
    if (!disk || !*disk || !number) return 1;
    char name[48];
    int k = snprintf(name, sizeof name, "%s%s%u", disk,
                     (disk[strlen(disk) - 1] >= '0' &&
                      disk[strlen(disk) - 1] <= '9') ? "p" : "", number);
    if (k < 0 || (size_t)k >= sizeof name) return 1;
    return block_busy(root, name);
}

int mbr_snapshot_busy_at(const char *root, const char *disk,
                         const DiskSnapshot *snapshot) {
    if (!disk || !*disk || !snapshot || block_busy(root, disk)) return 1;
    if (snapshot->kind != DS_TABLE_MBR ||
        snapshot->partition_count > DS_MAX_PARTITIONS) return 1;
    for (unsigned i = 0; i < snapshot->partition_count; i++) {
        char name[48];
        int k = snprintf(name, sizeof name, "%s%s%u", disk,
                         (disk[strlen(disk) - 1] >= '0' &&
                          disk[strlen(disk) - 1] <= '9') ? "p" : "",
                         snapshot->partitions[i].number);
        if (k < 0 || (size_t)k >= sizeof name || block_busy(root, name)) return 1;
    }
    return 0;
}
