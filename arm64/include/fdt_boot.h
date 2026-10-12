/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_FDT_BOOT_H
#define SEABIOS_ARM64_FDT_BOOT_H
#include <stdint.h>
#define ARM64_FDT_BOOTARG_MAX 255u

/* Rebuild /chosen while preserving existing devices and memory reservations.
 * The result lives in firmware-owned reserved memory and is valid until jump.
 * A missing initrd is represented by start=end=0. */
int arm64_fdt_prepare(const void *source, const char *bootargs,
                      uint64_t initrd_start, uint64_t initrd_end,
                      const void **prepared, uint32_t *length);
#endif
