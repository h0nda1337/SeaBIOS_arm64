/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/linuximage.h"
#include <stddef.h>

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t u64le(const uint8_t *p)
{
    return (uint64_t)u32le(p) | ((uint64_t)u32le(p + 4) << 32);
}

int arm64_linux_image_parse(const uint8_t *sector, uint64_t partition_sectors,
                           struct arm64_linux_image *result)
{
    if (!sector || !result || !partition_sectors)
        return -1;
    /* Linux arm64 boot image magic 'ARM\x64' at offset 0x38. */
    if (u32le(sector + 0x38) != UINT32_C(0x644d5241))
        return -2;
    uint64_t text_offset = u64le(sector + 8);
    uint64_t image_size = u64le(sector + 16);
    uint64_t flags = u64le(sector + 24);
    /* Must be a self-contained, bounded kernel Image, not a compressed
     * Image.gz, PE/COFF EFI app, legacy zero-size header, or unknown format. */
    if (text_offset >= 2u * 1024u * 1024u || (text_offset & 3u) ||
        image_size < 512u || image_size > 64u * 1024u * 1024u ||
        (flags & 1u) || /* little-endian only */
        (image_size + 511u) / 512u > partition_sectors)
        return -3;
    result->text_offset = text_offset;
    result->image_size = image_size;
    result->flags = flags;
    return 0;
}
