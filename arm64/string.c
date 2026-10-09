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

/* Explicit freestanding definitions, needed by src/romfile.c. */
void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (size_t i = 0; i < count; ++i)
        d[i] = s[i];
    return dest;
}

void *memset(void *dest, int c, size_t count)
{
    unsigned char *d = dest;
    for (size_t i = 0; i < count; ++i)
        d[i] = (unsigned char)c;
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < count; ++i)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strtcpy(char *dst, const char *src, size_t len)
{
    size_t n = 0;
    if (!len)
        return dst;
    while (src[n] && n < len - 1) {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
    return dst;
}
