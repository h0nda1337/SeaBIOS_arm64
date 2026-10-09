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

void *memmove(void *dest, const void *src, size_t count);
#endif
