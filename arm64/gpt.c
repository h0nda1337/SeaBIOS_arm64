/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/gpt.h"

/* Read-only bounded GPT validation for the first Linux filesystem partition.
 * Use strict checks and reject the complete table on invalid CRC, rather
 * than trusting arbitrary guest disk bytes as memory addresses. */
#define GPT_ENTRY_BYTES 128u
#define MAX_GPT_ENTRIES 128u
static uint8_t Header[512];
static uint8_t EntrySector[512];
static const uint8_t LinuxGuid[16] = {
    0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,
    0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4
};
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p)
{
    return le32(p) | ((uint64_t)le32(p + 4) << 32);
}
int arm64_gpt_find_linux_at(uint32_t index, uint64_t *first_lba,
                            uint64_t *sector_count)
{
    uint64_t capacity = arm64_virtio_blk_capacity_at(index);
    if (!first_lba || !sector_count || capacity < 34 ||
        arm64_virtio_blk_read_at(index, 1, Header))
        return -1;
    static const char magic[] = "EFI PART";
    for (unsigned i = 0; i < 8; ++i)
        if (Header[i] != (uint8_t)magic[i])
            return -1;
    unsigned header_size = le32(Header + 12);
    uint32_t saved_crc = le32(Header + 16);
    if (header_size < 92 || header_size > sizeof(Header) ||
        le64(Header + 24) != 1 || le64(Header + 32) >= capacity)
        return -2;
    for (unsigned i = 16; i < 20; ++i)
        Header[i] = 0;
    uint32_t crc = arm64_crc32(Header, header_size, UINT32_C(0xffffffff)) ^
                   UINT32_C(0xffffffff);
    if (crc != saved_crc)
        return -3;
    uint64_t first_usable = le64(Header + 40);
    uint64_t last_usable = le64(Header + 48);
    uint64_t entries_lba = le64(Header + 72);
    uint32_t entries = le32(Header + 80);
    uint32_t entry_size = le32(Header + 84);
    uint32_t array_crc = le32(Header + 88);
    if (!entries || entries > MAX_GPT_ENTRIES ||
        entry_size != GPT_ENTRY_BYTES || entries_lba < 2 ||
        entries_lba >= capacity ||
        first_usable < 34 || first_usable > last_usable ||
        last_usable >= capacity || (entries % 4) != 0 ||
        (uint64_t)entries * entry_size / 512 > capacity - entries_lba ||
        entries_lba + (uint64_t)entries * entry_size / 512 > first_usable)
        return -4;
    /* Full array CRC is mandatory: don't select a partition before that. */
    uint32_t aggregate_crc = UINT32_C(0xffffffff);
    uint64_t found_first = 0;
    uint64_t found_size = 0;
    for (uint32_t n = 0; n < entries / 4; ++n) {
        if (arm64_virtio_blk_read_at(index, entries_lba + n, EntrySector))
            return -5;
        aggregate_crc = arm64_crc32(EntrySector, 512, aggregate_crc);
        for (unsigned j = 0; j < 4; ++j) {
            const uint8_t *entry = EntrySector + j * GPT_ENTRY_BYTES;
            unsigned k = 0;
            for (; k < 16; ++k)
                if (entry[k] != LinuxGuid[k]) break;
            if (k != 16 || found_first)
                continue;
            uint64_t first = le64(entry + 32);
            uint64_t last = le64(entry + 40);
            if (last >= first && first >= first_usable &&
                last <= last_usable) {
                found_first = first;
                found_size = last - first + 1;
            }
        }
    }
    if ((aggregate_crc ^ UINT32_C(0xffffffff)) != array_crc)
        return -7;
    if (!found_first) return -8;
    *first_lba = found_first;
    *sector_count = found_size;
    return 0;
}

/* Original disk-0 API remains available to existing ARM64 consumers. */
int arm64_gpt_find_linux(uint64_t *first_lba, uint64_t *sector_count)
{
    return arm64_gpt_find_linux_at(0, first_lba, sector_count);
}
