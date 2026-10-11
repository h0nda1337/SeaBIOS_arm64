/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SB_ARM64_PE_LEGACY_H
#define SB_ARM64_PE_LEGACY_H
#include <stdint.h>
#define PE_LEGACY_BASE UINT64_C(0x4a000000)
/* PE32+ loader for explicitly native ARM64 no-import second-stage binaries.
 * NOT a Windows OS loader or a substitute for firmware protocols. */
int arm64_pe_legacy_map(const uint8_t *file, uint32_t size,
                        uint64_t *entry, uint32_t *mapped_size);
#endif
