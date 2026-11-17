/* mbr_busy.c — conservative read-only block-device use checks. */
#include "mbr_busy.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int path_join(char *out, size_t n, const char *root, const char *rel) {
    int k = snprintf(out, n, "%s%s", root ? root : "", rel);
    return k < 0 || (size_t)k >= n ? -1 : 0;
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
    int busy = 0;
    while (fgets(line, sizeof line, mounts)) {
        unsigned a, b;
        if (sscanf(line, "%*u %*u %u:%u", &a, &b) == 2 &&
            ((a == ma && b == mi) || (a == 0 && mount_source_is(line, name)))) {
            busy = 1; break;
        }
    }
    fclose(mounts);
    if (busy) return 1;

    if (path_join(path, sizeof path, root, "/proc/swaps")) return 1;
    FILE *swaps = fopen(path, "r");
    if (!swaps) return 1;
    if (!fgets(line, sizeof line, swaps)) busy = 1;
    while (!busy && fgets(line, sizeof line, swaps)) {
        char source[PATH_MAX];
        if (sscanf(line, "%4095s", source) != 1) continue;
        const char *leaf = strrchr(source, '/');
        leaf = leaf ? leaf + 1 : source;
        if (!strcmp(leaf, name)) { busy = 1; break; }
        struct stat st;
        if (!stat(source, &st) && S_ISBLK(st.st_mode) &&
            major(st.st_rdev) == ma && minor(st.st_rdev) == mi) {
            busy = 1; break;
        }
    }
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
