/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_BOOTORDER_H
#define SEABIOS_ARM64_BOOTORDER_H

/* The exact linked-list primitive is reused from the original SeaBIOS. */
#include "../../src/list.h"

struct arm64_boot_target {
    struct hlist_node link;
    int priority;
    const char *name;
    int (*attempt)(void *context);
    void *context;
};

void arm64_boot_register(struct arm64_boot_target *target);
void arm64_boot_execute(void);
#endif
