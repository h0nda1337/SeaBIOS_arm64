/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_BOOTORDER_H
#define SEABIOS_ARM64_BOOTORDER_H

/* Original SeaBIOS IPL record, registration and stable list insertion. */
#include "../../src/bootlist.h"
#include <stdint.h>

struct arm64_boot_target {
    struct bootentry_s entry;
    /* Relative fallback policy for this architecture's boot mechanisms.
     * The shared entry records the effective fw_cfg/default priority. */
    int priority;
    const char *name;
    /* Upstream fw_cfg bootorder glob; NULL for non-device candidates. */
    const char *firmware_path;
    int (*attempt)(void *context);
    void *context;
};

void arm64_boot_register(struct arm64_boot_target *target);
void arm64_boot_execute(void);
int arm64_bootprio_find(const char *path);
void arm64_boot_set_disk_path(const char *path);
void arm64_boot_set_disk_path_at(uint32_t index, const char *path);
const char *arm64_boot_disk_path(uint32_t index);
#endif
