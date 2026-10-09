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

struct fdt_info {
    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t uart_base;
    uint32_t dtb_bytes;
    int has_ram;
    int has_uart;
};

int fdt_probe(const void *dtb, struct fdt_info *result);
void uart_init(uint64_t base);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(uint64_t n);
void uart_dec(uint64_t n);
void uart_newline(void);
void arm64_post_memory(const struct fdt_info *info, const void *dtb);
void boot_main(void *dtb) __attribute__((noreturn));
void arm64_handoff(void *entry, void *dtb) __attribute__((noreturn));
#endif
