/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_FWCFG_H
#define SEABIOS_ARM64_FWCFG_H
#include <stdint.h>
/* Register QEMU fw_cfg MMIO entries through upstream src/romfile.c. */
int arm64_fwcfg_init(uint64_t base);
void arm64_fwcfg_report(void);
#endif
