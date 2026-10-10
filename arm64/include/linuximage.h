/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_LINUXIMAGE_H
#define SEABIOS_ARM64_LINUXIMAGE_H
#include <stdint.h>

struct arm64_linux_image {
    uint64_t text_offset;
    uint64_t image_size;
    uint64_t flags;
};
/* Only parse standard little-endian ARM64 Image headers with known size.
 * This function is intentionally freestanding for host fuzz/unit tests. */
int arm64_linux_image_parse(const uint8_t *sector, uint64_t partition_sectors,
                           struct arm64_linux_image *result);
#endif
