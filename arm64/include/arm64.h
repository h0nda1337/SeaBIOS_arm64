/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_H
#define SEABIOS_ARM64_H

#include <stdint.h>
#include <stddef.h>

/* Minimal bring-up interface.  Not an implementation of PC BIOS or UEFI. */
#define VIRT_RAM_BASE       UINT64_C(0x40000000)
#define VIRT_DTB_ADDRESS    UINT64_C(0x40000000)
#define VIRT_PL011_FALLBACK UINT64_C(0x09000000)
#define VIRT_PAYLOAD_BASE   UINT64_C(0x48000000)
#define VIRT_PAYLOAD_MAX    (1024u * 1024u)

#define ARM64_MAX_RAM_BANKS 8
#define ARM64_MAX_RESERVED_REGIONS 24
#define ARM64_MAX_VIRTIO_MMIO 32
struct fdt_region {
    uint64_t base;
    uint64_t size;
};

struct fdt_info {
    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t uart_base;
    uint32_t dtb_bytes;
    int has_ram;
    int has_uart;
    uint64_t fwcfg_base;
    int has_fwcfg;
    uint32_t ram_count;
    uint32_t reserved_count;
    struct fdt_region ram[ARM64_MAX_RAM_BANKS];
    struct fdt_region reserved[ARM64_MAX_RESERVED_REGIONS];
    uint32_t virtio_count;
    struct fdt_region virtio_mmio[ARM64_MAX_VIRTIO_MMIO];
};

int fdt_probe(const void *dtb, struct fdt_info *result);
/* FDT header memreserve table, not the x86 E820 boot protocol. */
typedef int (*fdt_reservation_fn)(uint64_t start, uint64_t length, void *ctx);
int fdt_for_each_reservation(const void *dtb,
                             fdt_reservation_fn fn, void *ctx);
void uart_init(uint64_t base);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(uint64_t n);
void uart_dec(uint64_t n);
void uart_newline(void);
void arm64_report_cpu(void);
void arm64_virtio_discover(const struct fdt_info *info);
int arm64_post_memory(const struct fdt_info *info, const void *dtb);
int arm64_post_can_load(uint64_t start, uint64_t size);
void arm64_post_reserve_payload(uint64_t start, uint64_t size);
void boot_main(void *dtb) __attribute__((noreturn));
void arm64_handoff(void *entry, void *dtb) __attribute__((noreturn));
#endif
