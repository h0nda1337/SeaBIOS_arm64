/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/seabios_compat.h"

/* POST-only fixed-lifetime arena. Separated from the QEMU payload, DTB and
 * E820 reported usable RAM. No implicit return to RAM after chainload. */
#define BOOT_HEAP_BYTES (64u * 1024u)
static unsigned char BootHeap[BOOT_HEAP_BYTES] __attribute__((aligned(16)));
static size_t heap_used;

static void *boot_alloc(size_t size)
{
    if (!size || size > BOOT_HEAP_BYTES)
        return NULL;
    const size_t alignment = 16u;
    if (heap_used > BOOT_HEAP_BYTES - (alignment - 1))
        return NULL;
    size_t offset = (heap_used + alignment - 1) & ~(alignment - 1);
    if (size > BOOT_HEAP_BYTES - offset)
        return NULL;
    heap_used = offset + size;
    return &BootHeap[offset];
}

void *malloc_tmp(size_t size) { return boot_alloc(size); }
void *malloc_tmphigh(size_t size) { return boot_alloc(size); }
/* Boot-time allocation lifetime. src/romfile.c calls free only on failure.
 * The arena is reclaimed wholesale when the firmware is replaced. */
void free(void *ptr) { (void)ptr; }
