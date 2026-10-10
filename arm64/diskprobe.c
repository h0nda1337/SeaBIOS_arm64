/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* Read-only diagnostics; not yet a filesystem or a bootable disk manager. */
static uint8_t Sector0[512];
static uint8_t Sector1[512];
static const char TestMarker[] = "SBARM64!";

void arm64_disk_probe(void)
{
    if (!arm64_virtio_blk_capacity())
        return;
    if (arm64_virtio_blk_read(0, Sector0)) {
        uart_puts("[disk] LBA 0 read failed\n");
        return;
    }
    uart_puts("[disk] LBA 0 read OK\n");
    if (Sector0[510] == 0x55 && Sector0[511] == 0xaa)
        uart_puts("[disk] MBR signature 55AA\n");
    int marker = 1;
    for (unsigned i = 0; i < sizeof(TestMarker) - 1; ++i) {
        if (Sector0[256 + i] != (uint8_t)TestMarker[i])
            marker = 0;
    }
    if (marker)
        uart_puts("[disk] diagnostic LBA 0 marker OK\n");
    if (arm64_virtio_blk_capacity() < 2)
        return;
    if (arm64_virtio_blk_read(1, Sector1)) {
        uart_puts("[disk] LBA 1 read failed\n");
        return;
    }
    uart_puts("[disk] LBA 1 read OK\n");
    static const char gpt[] = "EFI PART";
    int gpt_header = 1;
    for (unsigned i = 0; i < sizeof(gpt) - 1; ++i)
        if (Sector1[i] != (uint8_t)gpt[i])
            gpt_header = 0;
    if (gpt_header)
        uart_puts("[disk] GPT primary header signature detected\n");
    if (marker && Sector1[0] == 0x5a && Sector1[1] == 0xa5)
        uart_puts("[disk] diagnostic LBA 1 marker OK\n");
}
