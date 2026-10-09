/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/bootorder.h"

extern void uart_puts(const char *text);

/* SeaBIOS hlist is used directly; a future step should port the boot.c
 * boot-priority logic itself rather than duplicate it here. */
static struct hlist_head BootTargets;

void arm64_boot_register(struct arm64_boot_target *target)
{
    struct hlist_node **link = &BootTargets.first;
    while (*link) {
        struct arm64_boot_target *current =
            container_of(*link, struct arm64_boot_target, link);
        if (target->priority < current->priority)
            break;
        link = &(*link)->next;
    }
    hlist_add(&target->link, link);
}

void arm64_boot_execute(void)
{
    struct arm64_boot_target *target;
    hlist_for_each_entry(target, &BootTargets, link) {
        uart_puts("[boot] probing: ");
        uart_puts(target->name);
        uart_puts("\n");
        int status = target->attempt(target->context);
        if (status == 0)
            uart_puts("[boot] not found\n");
        else if (status < 0)
            uart_puts("[boot] invalid or not supported\n");
        /* A successful handoff is non-returning; positive is an error. */
        else
            uart_puts("[boot] handoff unexpectedly returned\n");
    }
}
