/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/bootorder.h"
#include "include/arm64.h"
#include "../src/romfile.h"
#include "../src/bootprio.h"

extern void uart_puts(const char *text);

/* Bootorder matching comes from the same source as the original x86
 * SeaBIOS; the AArch64 boot *mechanisms* are still architecture-specific. */
static struct hlist_head BootTargets;
static const char *DiskPath;

void arm64_boot_set_disk_path(const char *path) { DiskPath = path; }

static char BootorderContents[4096];
static unsigned BootorderLength;
static int BootorderLoaded;

static void bootprio_init(void)
{
    if (BootorderLoaded)
        return;
    BootorderLoaded = 1;
    struct romfile_s *file = romfile_find("bootorder");
    if (!file || !file->size || file->size > sizeof(BootorderContents))
        return;
    int copied = file->copy(file, BootorderContents, sizeof(BootorderContents));
    if (copied != (int)file->size)
        return;
    BootorderLength = file->size;
    uart_puts("[boot] shared SeaBIOS fw_cfg bootorder enabled\n");
}

int arm64_bootprio_find(const char *path)
{
    bootprio_init();
    if (!BootorderLength || !path)
        return -1;
    return bootprio_find_text(path, BootorderContents, BootorderLength);
}


void arm64_boot_register(struct arm64_boot_target *target)
{
    if (!target->firmware_path && target->priority < 10)
        target->firmware_path = DiskPath;
    if (target->firmware_path) {
        uart_puts("[boot] device firmware path: ");
        uart_puts(target->firmware_path);
        uart_puts("\n");
        int rank = arm64_bootprio_find(target->firmware_path);
        if (rank >= 0) {
            target->priority = rank;
            uart_puts("[boot] fw_cfg priority matched: ");
            uart_puts(target->name);
            uart_puts("\n");
        }
    }
    struct hlist_node **link = &BootTargets.first;
    while (*link) {
        struct arm64_boot_target *current =
            container_of(*link, struct arm64_boot_target, link);
        /* SeaBIOS x86's actual IPL sorting policy. Disk-like options
         * precede non-disk diagnostic options when priorities tie. */
        struct bootprio_key incoming = {
            .priority = target->priority,
            .type = target->firmware_path ? 2 : 0x80,
        };
        struct bootprio_key existing = {
            .priority = current->priority,
            .type = current->firmware_path ? 2 : 0x80,
        };
        if (bootprio_precedes(&incoming, &existing))
            break;
        link = &(*link)->next;
    }
    hlist_add(&target->link, link);
}

void arm64_boot_execute(void)
{
    /* SeaBIOS x86's HALT entry means strict bootorder: do not attempt
     * fallback targets that appear after the HALT priority. */
    const int halt_rank = arm64_bootprio_find("HALT");
    struct arm64_boot_target *target;
    hlist_for_each_entry(target, &BootTargets, link) {
        if (halt_rank >= 0 && target->priority >= halt_rank) {
            uart_puts("[boot] strict SeaBIOS bootorder HALT reached\n");
            break;
        }
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
