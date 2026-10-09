/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

/* Freestanding ARM64 adapter for SeaBIOS's generic map code.  Do not use
 * its x86 string.c, which contains x86 segment and rep instructions. */
void *memmove(void *dest, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if ((uintptr_t)d <= (uintptr_t)s) {
        for (size_t i = 0; i < count; ++i)
            d[i] = s[i];
    } else {
        while (count) {
            --count;
            d[count] = s[count];
        }
    }
    return dest;
}
