/* mbr_mount_plan.c — pure dry-run plan builder; deliberately no device I/O. */
#include "mbr_mount_plan.h"

#include <stdio.h>
#include <string.h>

static const DsPartition *part_find(const DiskSnapshot *d, unsigned number) {
    for (unsigned i = 0; i < d->partition_count; i++)
        if (d->partitions[i].number == number) return &d->partitions[i];
    return NULL;
}

static int uuid_valid(const char *s) {
    if (!s || strlen(s) != 36) return 0;
    for (unsigned i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return 0;
        } else if (!((s[i] >= '0' && s[i] <= '9') ||
                     (s[i] >= 'a' && s[i] <= 'f') ||
                     (s[i] >= 'A' && s[i] <= 'F'))) return 0;
    }
    return 1;
}

static int add_action(MpPlan *p, MpActionKind kind, unsigned part,
                      unsigned size, const char *target, const char *fs) {
    if (p->action_count >= MP_MAX_ACTIONS ||
        strlen(target) >= sizeof p->actions[0].target ||
        strlen(fs) >= sizeof p->actions[0].filesystem) return -1;
    MpAction *a = &p->actions[p->action_count++];
    a->kind = kind; a->partition = part; a->size_mib = size;
    strcpy(a->target, target); strcpy(a->filesystem, fs);
    return 0;
}

static int add_fstab(MpPlan *p, const char *line) {
    if (p->fstab_count >= 4 || strlen(line) >= MP_FSTAB_MAX) return -1;
    strcpy(p->fstab[p->fstab_count++], line);
    return 0;
}

int mp_build(const DiskSnapshot *d, const MmPlan *s,
             int bios_booted, int disk_busy,
             const char *home_fs, const char *home_uuid, MpPlan *out) {
    if (!d || !s || !out || d->kind != DS_TABLE_MBR ||
        d->logical_sector_size != 512 || !bios_booted || disk_busy ||
        !d->total_sectors || d->partition_count > DS_MAX_PARTITIONS ||
        !s->root_part || s->root_format != 1 ||
        (s->home_format != 0 && s->home_format != 1) ||
        s->root_part == s->home_part || s->root_part == s->swap_part ||
        (s->home_part && s->home_part == s->swap_part) ||
        (s->swap_part && s->swap_file_mib) || s->swap_file_mib > 16384)
        return -1;

    const DsPartition *root = part_find(d, s->root_part);
    if (!root || root->type_code != 0x83 ||
        root->first_lba > root->last_lba ||
        root->last_lba >= d->total_sectors ||
        (root->last_lba - root->first_lba + 1) < (MM_ROOT_MIN_BYTES / 512))
        return -1;

    const DsPartition *home = NULL, *swap = NULL;
    if (s->home_part) {
        home = part_find(d, s->home_part);
        if (!home || home->type_code != 0x83 ||
            home->first_lba > home->last_lba ||
            home->last_lba >= d->total_sectors ||
            (home->last_lba - home->first_lba + 1) < (MM_HOME_MIN_BYTES / 512))
            return -1;
        if (!s->home_format &&
            (!mm_home_fs_ok(home_fs) || !uuid_valid(home_uuid))) return -1;
    }
    if (s->swap_part) {
        swap = part_find(d, s->swap_part);
        if (!swap || swap->type_code != 0x82 ||
            swap->first_lba > swap->last_lba ||
            swap->last_lba >= d->total_sectors ||
            (swap->last_lba - swap->first_lba + 1) < (MM_SWAP_MIN_BYTES / 512) ||
            (swap->last_lba - swap->first_lba + 1) > (MM_SWAP_MAX_BYTES / 512))
            return -1;
    }

    MpPlan p;
    memset(&p, 0, sizeof p);
    if (add_action(&p, MP_FORMAT_ROOT_EXT4, s->root_part, 0, "/", "ext4") ||
        add_action(&p, MP_MOUNT_ROOT, s->root_part, 0, "/", "ext4") ||
        add_fstab(&p, "UUID=@ROOT_UUID_AFTER_FORMAT@ / ext4 defaults 0 1"))
        return -1;

    if (home) {
        if (s->home_format) {
            if (add_action(&p, MP_FORMAT_HOME_EXT4, s->home_part, 0, "/home", "ext4") ||
                add_action(&p, MP_MOUNT_HOME_PRESERVE, s->home_part, 0, "/home", "ext4") ||
                add_fstab(&p, "UUID=@HOME_UUID_AFTER_FORMAT@ /home ext4 defaults 0 2"))
                return -1;
        } else {
            char line[MP_FSTAB_MAX];
            int n = snprintf(line, sizeof line, "UUID=%s /home %s defaults 0 2",
                             home_uuid, home_fs);
            if (n < 0 || (size_t)n >= sizeof line ||
                add_action(&p, MP_MOUNT_HOME_PRESERVE, s->home_part, 0, "/home", home_fs) ||
                add_fstab(&p, line)) return -1;
        }
    }

    if (swap) {
        if (add_action(&p, MP_MKSWAP_PARTITION, s->swap_part, 0, "swap", "swap") ||
            add_fstab(&p, "UUID=@SWAP_UUID_AFTER_MKSWAP@ none swap sw 0 0"))
            return -1;
    } else if (s->swap_file_mib) {
        char line[MP_FSTAB_MAX];
        int n = snprintf(line, sizeof line, "/swapfile none swap sw 0 0");
        if (n < 0 || (size_t)n >= sizeof line ||
            add_action(&p, MP_CREATE_SWAPFILE, 0, s->swap_file_mib,
                       "/swapfile", "swap") || add_fstab(&p, line)) return -1;
    }

    *out = p;
    return 0;
}
