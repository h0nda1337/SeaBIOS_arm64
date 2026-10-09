/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* PrimeCell PL011 register byte offsets. */
#define PL011_FR 0x18
#define PL011_CR 0x30
#define PL011_FR_TXFF (1u << 5)

static uint64_t uart_mmio;

static volatile uint32_t *reg32(unsigned offset)
{
    return (volatile uint32_t *)(uintptr_t)(uart_mmio + offset);
}

void uart_init(uint64_t base)
{
    uart_mmio = base;
    /* Uart clock/baud are configured by QEMU; ensure UART/TX/RX enabled. */
    *reg32(PL011_CR) |= 0x301u;
}

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');
    while (*reg32(PL011_FR) & PL011_FR_TXFF) { }
    *reg32(0) = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_newline(void) { uart_putc('\n'); }

void uart_hex(uint64_t n)
{
    static const char digits[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        uart_putc(digits[(n >> i) & 15]);
}

void uart_dec(uint64_t n)
{
    char buf[20];
    unsigned pos = 0;
    do {
        buf[pos++] = '0' + (char)(n % 10);
        n /= 10;
    } while (n && pos < sizeof(buf));
    while (pos)
        uart_putc(buf[--pos]);
}
