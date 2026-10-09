/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "../src/e820map.h"

extern unsigned char __data_begin[];
extern unsigned char __stack_top[];

static int contains(uint64_t base, uint64_t length, uint64_t start,
                    uint64_t amount)
{
    if (!amount || start < base || start - base > length)
        return 0;
    return amount <= length - (start - base);
}

/* POST phase scaffold inspired by src/post.c.  It does not call upstream
 * maininit(): that code still requires x86 IVT/BDA/EBDA, PCI and INT19. */
void arm64_post_memory(const struct fdt_info *info, const void *dtb)
{
    uart_puts("[post] SeaBIOS memory map (src/e820map.c)\n");
    e820_count = 0;
    if (!info->has_ram || !info->ram_size ||
        info->ram_size > UINT64_MAX - info->ram_base) {
        uart_puts("[post] no valid RAM span\n");
        return;
    }

    e820_add(info->ram_base, info->ram_size, E820_RAM);

    /* Keep DTB, state, BSS and firmware stack unavailable for later loading. */
    uint64_t dtb_start = (uint64_t)(uintptr_t)dtb;
    uint64_t dtb_len = (info->dtb_bytes + 4095u) & ~UINT64_C(4095);
    if (contains(info->ram_base, info->ram_size, dtb_start, dtb_len))
        e820_add(dtb_start, dtb_len, E820_RESERVED);

    uint64_t private_start = (uint64_t)(uintptr_t)__data_begin;
    uint64_t private_end = (uint64_t)(uintptr_t)__stack_top;
    if (private_end > private_start &&
        contains(info->ram_base, info->ram_size, private_start,
                 private_end - private_start))
        e820_add(private_start, private_end - private_start, E820_RESERVED);

    /* This map follows the SeaBIOS E820 algorithm, but does NOT implement
     * the x86 E820 firmware-call interface on AArch64. */
    uart_puts("[post] regions: ");
    uart_dec((uint32_t)e820_count);
    uart_newline();
    for (int i = 0; i < e820_count; ++i) {
        const struct e820entry *region = &e820_list[i];
        uart_puts("[post]   ");
        uart_hex(region->start);
        uart_puts(" + ");
        uart_hex(region->size);
        uart_puts(region->type == E820_RAM ? " RAM\n" : " reserved\n");
    }
}
