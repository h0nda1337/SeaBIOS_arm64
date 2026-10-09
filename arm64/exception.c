/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* Synchronous faults, IRQ, FIQ, and SError currently abort. No GIC or
 * scheduler exists, so silently returning would hide critical corruption. */
void arm64_exception_report(uint64_t level, uint64_t syndrome,
                            uint64_t elr, uint64_t fault_address)
                            __attribute__((noreturn));

void arm64_exception_report(uint64_t level, uint64_t syndrome,
                            uint64_t elr, uint64_t fault_address)
{
    /* The diagnostic console is intentionally QEMU virt-specific. */
    uart_init(VIRT_PL011_FALLBACK);
    uart_puts("\n[PANIC] unexpected AArch64 exception\n  EL: ");
    uart_dec(level);
    uart_puts("\n  ESR: ");
    uart_hex(syndrome);
    uart_puts("\n  ELR: ");
    uart_hex(elr);
    uart_puts("\n  FAR: ");
    uart_hex(fault_address);
    uart_puts("\n[PANIC] CPU halted\n");
    for (;;)
        __asm__ volatile("wfe");
}

void arm64_report_cpu(void)
{
    uint64_t el, midr;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    uart_puts("[cpu] AArch64 exception level: ");
    uart_dec(el >> 2);
    uart_puts(" / MIDR_EL1: ");
    uart_hex(midr);
    uart_newline();
}
