/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "../src/e820map.h"

extern unsigned char __data_begin[];
extern unsigned char __stack_top[];

struct reserve_context {
    const struct fdt_info *info;
};

/* Reserve the intersections with every reported RAM bank. Without clipping,
 * reserved MMIO could be inserted as if it were a RAM region. */
static int reserve_fdt_entry(uint64_t start, uint64_t amount, void *opaque)
{
    const struct reserve_context *context = opaque;
    if (!amount || start > UINT64_MAX - amount)
        return -1;
    uint64_t end = start + amount;
    for (uint32_t i = 0; i < context->info->ram_count; ++i) {
        const struct fdt_region *bank = &context->info->ram[i];
        uint64_t bank_end = bank->base + bank->size;
        if (end <= bank->base || start >= bank_end)
            continue;
        uint64_t from = start < bank->base ? bank->base : start;
        uint64_t to = end > bank_end ? bank_end : end;
        /* e820_add() can split one map entry into three; preserve headroom
         * and reject unrepresentable maps rather than silently omit entries. */
        if (e820_count > 124)
            return -2;
        e820_add(from, to - from, E820_RESERVED);
    }
    return 0;
}

/* POST phase scaffold inspired by src/post.c.  It does not call upstream
 * maininit(): that code still requires x86 IVT/BDA/EBDA, PCI and INT19. */
int arm64_post_memory(const struct fdt_info *info, const void *dtb)
{
    uart_puts("[post] SeaBIOS memory map (src/e820map.c)\n");
    e820_count = 0;
    if (!info->has_ram || !info->ram_count ||
        info->ram_count > ARM64_MAX_RAM_BANKS)
        return -1;

    for (uint32_t i = 0; i < info->ram_count; ++i) {
        uint64_t base = info->ram[i].base;
        uint64_t bytes = info->ram[i].size;
        if (!bytes || base > UINT64_MAX - bytes || e820_count > 124) {
            uart_puts("[post] invalid RAM bank\n");
            return -1;
        }
        e820_add(base, bytes, E820_RAM);
    }

    struct reserve_context context = { info };
    int status = fdt_for_each_reservation(dtb, reserve_fdt_entry, &context);
    if (status < 0) {
        uart_puts("[post] invalid FDT reservation table\n");
        return -2;
    }
    /* /reserved-memory child nodes are distinct from header memreserve
     * entries and must also be excluded from loader allocations. */
    for (uint32_t i = 0; i < info->reserved_count; ++i) {
        const struct fdt_region *entry = &info->reserved[i];
        if (reserve_fdt_entry(entry->base, entry->size, &context))
            return -3;
    }

    /* Keep DTB, writable firmware state, heap, BSS and stack unavailable. */
    uint64_t dtb_start = (uint64_t)(uintptr_t)dtb;
    uint64_t dtb_len = (info->dtb_bytes + 4095u) & ~UINT64_C(4095);
    if (reserve_fdt_entry(dtb_start, dtb_len, &context))
        return -4;
    uint64_t private_start = (uint64_t)(uintptr_t)__data_begin;
    uint64_t private_end = (uint64_t)(uintptr_t)__stack_top;
    if (private_end <= private_start ||
        reserve_fdt_entry(private_start, private_end - private_start,
                          &context))
        return -5;

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
    return 0;
}

/* E820 describes internal firmware reservations on ARM, not INT 15h. */
int arm64_post_can_load(uint64_t start, uint64_t size)
{
    if (!size || start > UINT64_MAX - size)
        return 0;
    for (int i = 0; i < e820_count; ++i) {
        const struct e820entry *region = &e820_list[i];
        if (region->type == E820_RAM && start >= region->start &&
            start - region->start <= region->size &&
            size <= region->size - (start - region->start))
            return 1;
    }
    return 0;
}

void arm64_post_reserve_payload(uint64_t start, uint64_t size)
{
    if (arm64_post_can_load(start, size))
        e820_add(start, size, E820_RESERVED);
}
