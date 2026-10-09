/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_AARCH64_COMPAT_H
#define SEABIOS_AARCH64_COMPAT_H

/* Temporary bridge for compiling *unmodified algorithms* from src/ on ARM64.
 * These are NOT substitutes for the full SeaBIOS runtime/16-bit ABI. */
#define BUILD_MAX_E820 128
/* No x86 F-segment exists on ARM. Variables reside in normal RAM. */
#undef VARFSEG
#define VARFSEG

/* The full SeaBIOS output subsystem depends on x86 conventions.
 * Route debug printf calls to a temporary sink; ARM post prints the map. */
void arm64_debug_sink(int level, const char *fmt, ...);
void arm64_warn_noalloc(void);
#define dprintf(lvl, fmt, ...) arm64_debug_sink((lvl), (fmt), ##__VA_ARGS__)
#define warn_noalloc() arm64_warn_noalloc()

/* Freestanding services consumed by genuine src/romfile.c, not a ROM-file
 * reimplementation. Fixed-lifetime boot allocator (no general-purpose free).
 * Do not include the x86 malloc.h or string.h in AArch64 code. */
void *malloc_tmp(size_t size);
void *malloc_tmphigh(size_t size);
void free(void *pointer);
void *memmove(void *dest, const void *src, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *dest, int c, size_t count);
int memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strtcpy(char *dst, const char *src, size_t len);
#endif
