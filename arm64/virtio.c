/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* Initial discovery only. A real virtio transport/queue driver will need to
 * reuse and adapt src/hw/virtio-mmio.c and src/hw/virtio-ring.c. These
 * probe reads are NOT device initialization or block I/O. */
#define VIRTIO_MMIO_MAGIC       0x000u
#define VIRTIO_MMIO_VERSION     0x004u
#define VIRTIO_MMIO_DEVICE_ID   0x008u
#define VIRTIO_MMIO_VENDOR_ID   0x00cu
#define VIRTIO_MAGIC           0x74726976u

static uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    return *(const volatile uint32_t *)(uintptr_t)(base + offset);
}

void arm64_virtio_discover(const struct fdt_info *info)
{
    uart_puts("[virtio] MMIO transports in DTB: ");
    uart_dec(info->virtio_count);
    uart_newline();
    for (uint32_t i = 0; i < info->virtio_count; ++i) {
        const struct fdt_region *region = &info->virtio_mmio[i];
        if (region->base & 3u || region->base > UINTPTR_MAX - 0x100u)
            continue;
        uint32_t magic = mmio_read32(region->base, VIRTIO_MMIO_MAGIC);
        if (magic != VIRTIO_MAGIC)
            continue;
        uint32_t version = mmio_read32(region->base, VIRTIO_MMIO_VERSION);
        uint32_t device = mmio_read32(region->base, VIRTIO_MMIO_DEVICE_ID);
        uint32_t vendor = mmio_read32(region->base, VIRTIO_MMIO_VENDOR_ID);
        if (!device)
            continue;
        uart_puts("[virtio] slot: ");
        uart_hex(region->base);
        uart_puts("  version: ");
        uart_dec(version);
        uart_puts("  device: ");
        uart_dec(device);
        uart_puts("  vendor: ");
        uart_hex(vendor);
        uart_newline();
    }
}
