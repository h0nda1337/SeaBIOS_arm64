/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/gpt.h"
/* Standard CRC-32/ISO-HDLC (reflected), incremental state seed 0xffffffff. */
uint32_t arm64_crc32(const uint8_t *data, size_t size, uint32_t crc)
{
    if (!data) return crc;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return crc;
}
