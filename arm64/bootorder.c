/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/bootorder.h"
#include "include/arm64.h"
#include "../src/romfile.h"
#include "../src/bootprio.h"

extern void uart_puts(const char *text);

/* Bootorder matching comes from the same source as the original x86
 * SeaBIOS; the AArch64 boot *mechanisms* are still architecture-specific. */
static struct hlist_head BootTargets;
static struct bootentry_s BootHalt;
static int BootHaltRegistered;
static const char *DiskPath;
/* Each firmware pathname has static lifetime and belongs to its VirtIO disk. */
static char BootPaths[ARM64_MAX_BLOCK_DRIVES][32];

void arm64_boot_set_disk_path_at(uint32_t index, const char *path)
{
    if (index >= ARM64_MAX_BLOCK_DRIVES || !path)
        return;
    unsigned n = 0;
    while (n + 1 < sizeof(BootPaths[index]) && path[n]) {
        BootPaths[index][n] = path[n];
        ++n;
    }
    BootPaths[index][n] = 0;
}

const char *arm64_boot_disk_path(uint32_t index)
{
    return index < ARM64_MAX_BLOCK_DRIVES && BootPaths[index][0]
        ? BootPaths[index] : NULL;
}


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
    if (!target || !target->attempt)
        return;
    if (!target->firmware_path && target->priority < 10)
        target->firmware_path = DiskPath;
    /* fw_cfg ranks start at 1; the legacy ARM-only priorities (3/5/10)
     * must not compete with explicit device ranks. Keep fallback mechanisms
     * in their existing relative order after all bounded bootorder ranks. */
    int priority = BOOT_PRIO_DEFAULT;
    if (target->priority > 0 && target->priority <= BOOT_PRIO_DEFAULT)
        priority += target->priority;
    if (target->firmware_path) {
        uart_puts("[boot] device firmware path: ");
        uart_puts(target->firmware_path);
        uart_puts("\n");
        int rank = arm64_bootprio_find(target->firmware_path);
        if (rank >= 0) {
            priority = rank;
            uart_puts("[boot] fw_cfg priority matched: ");
            uart_puts(target->name ?: "?");
            uart_puts("\n");
        }
    }
    /* Bind the original IPL record to the actual registered SeaBIOS drive,
     * so equal priorities use drive->type/cntl_id, just as on x86. */
    target->entry.drive = NULL;
    for (uint32_t disk = 0; disk < ARM64_MAX_BLOCK_DRIVES; ++disk)
        if (target->firmware_path &&
            target->firmware_path == arm64_boot_disk_path(disk)) {
            target->entry.drive = arm64_block_drive_at(disk);
            break;
        }
    bootlist_add(&BootTargets, &target->entry,
                  target->firmware_path ? IPL_TYPE_HARDDISK : IPL_TYPE_BEV,
                  priority, target->name);
}

void arm64_boot_execute(void)
{
    /* Register the same IPL_TYPE_HALT sentinel as x86 bcv_prepboot(). It
     * participates in the original list sort, including type tie-breaking. */
    const int halt_rank = arm64_bootprio_find("HALT");
    if (halt_rank >= 0 && !BootHaltRegistered) {
        bootlist_add(&BootTargets, &BootHalt, IPL_TYPE_HALT, halt_rank, "HALT");
        BootHaltRegistered = 1;
    }
    struct bootentry_s *entry;
    hlist_for_each_entry(entry, &BootTargets, node) {
        if (entry->type == IPL_TYPE_HALT) {
            uart_puts("[boot] strict SeaBIOS bootorder HALT reached\n");
            break;
        }
        struct arm64_boot_target *target =
            container_of(entry, struct arm64_boot_target, entry);
        uart_puts("[boot] probing: ");
        uart_puts(entry->description);
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
