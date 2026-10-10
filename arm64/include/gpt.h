/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_AARCH64_GPT_H
#define SEABIOS_AARCH64_GPT_H
#include <stddef.h>
#include <stdint.h>
uint32_t arm64_crc32(const uint8_t *data, size_t size, uint32_t crc);
/* Return 0 only for a validated GPT and a Linux filesystem partition.
 * A Linux filesystem partition is NOT necessarily a bootable raw Image. */
int arm64_gpt_find_linux(uint64_t *first_lba, uint64_t *sector_count);
#endif
