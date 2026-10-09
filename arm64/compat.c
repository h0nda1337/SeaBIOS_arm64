/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include <stdarg.h>

/* SeaBIOS's full output.c depends on real-mode thunks and VGA BIOS state.
 * A bounded UART formatter lets the ORIGINAL src/e820map.c and
 * src/romfile.c diagnostics work while that output backend is being ported.
 * Only the formats used by these original modules are implemented. */
#define ARM64_LOG_LEVEL 3

static void put_unsigned(uint64_t value, unsigned base, unsigned min_width,
                         char pad)
{
    char buf[24];
    unsigned len = 0;
    static const char hex[] = "0123456789abcdef";
    do {
        buf[len++] = hex[value % base];
        value /= base;
    } while (value && len < sizeof(buf));
    while (min_width > len) {
        uart_putc(pad);
        --min_width;
    }
    while (len)
        uart_putc(buf[--len]);
}

void arm64_debug_sink(int level, const char *fmt, ...)
{
    if (level > ARM64_LOG_LEVEL || level < 0 || !fmt)
        return;
    va_list args;
    va_start(args, fmt);
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            uart_putc(*p);
            continue;
        }
        ++p;
        if (!*p)
            break;
        if (*p == '%') {
            uart_putc('%');
            continue;
        }
        char pad = ' ';
        unsigned width = 0;
        if (*p == '0') { pad = '0'; ++p; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (unsigned)(*p - '0');
            if (width > 32) width = 32;
            ++p;
        }
        int count_long = 0;
        while (*p == 'l' && count_long < 2) {
            ++count_long;
            ++p;
        }
        switch (*p) {
        case 'd': case 'i': {
            int64_t n = count_long == 2 ? va_arg(args, long long) :
                count_long == 1 ? va_arg(args, long) : va_arg(args, int);
            if (n < 0) {
                uart_putc('-');
                if (width) --width;
                /* Avoid signed overflow for INT64_MIN. */
                put_unsigned(0u - (uint64_t)n, 10, width, pad);
            } else {
                put_unsigned((uint64_t)n, 10, width, pad);
            }
            break;
        }
        case 'u': case 'x': case 'X': {
            uint64_t n = count_long == 2 ? va_arg(args, unsigned long long) :
                count_long == 1 ? va_arg(args, unsigned long) :
                va_arg(args, unsigned int);
            put_unsigned(n, *p == 'u' ? 10 : 16, width, pad);
            break;
        }
        case 'p':
            uart_puts("0x");
            put_unsigned((uintptr_t)va_arg(args, void *), 16, 16, '0');
            break;
        case 'c':
            uart_putc((char)va_arg(args, int));
            break;
        case 's': {
            const char *str = va_arg(args, const char *);
            uart_puts(str ? str : "(null)");
            break;
        }
        default:
            uart_puts("[format unsupported]");
            break;
        }
    }
    va_end(args);
}

void arm64_warn_noalloc(void)
{
    uart_puts("[seabios] firmware allocation or map entry limit reached\n");
}
