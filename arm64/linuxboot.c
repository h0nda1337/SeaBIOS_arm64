/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/linuximage.h"
#include "include/bootorder.h"
#include "include/gpt.h"

/* A minimal direct Linux Image handoff from a raw MBR/GPT Linux partition.
 * This deliberately omits filesystems, EFI, initrd, DTB relocation and
 * cryptographic signature checks. Only QEMU virt has been tested. */
#define LINUX_BASE UINT64_C(0x48000000) /* 2 MiB aligned inside QEMU RAM */
#define LINUX_MBR_PARTITION_TYPE 0x83u
#define SECTOR_BYTES 512u
static uint8_t Sector[SECTOR_BYTES];
static uint8_t ReadBatch[SECTOR_BYTES * 16u];
static uint8_t DiskMBR[SECTOR_BYTES];
static uint64_t LinuxFirstSector;
static uint64_t LinuxSectorCount;
static struct arm64_linux_image LinuxHeader;
static void *LinuxDtb;
static struct arm64_boot_target LinuxTarget;

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int boot_linux_image(void *context)
{
    (void)context;
    const uint64_t dest = LINUX_BASE + LinuxHeader.text_offset;
    const uint64_t size = LinuxHeader.image_size;
    /* These regions are checked against actual RAM and firmware reservations,
     * including the boot-time stack, DTB and QEMU flash mappings. */
    if (dest < LINUX_BASE || !arm64_post_can_load(dest, size)) {
        uart_puts("[linux] destination overlaps reserved memory\n");
        return -1;
    }
    const uint64_t blocks = (size + SECTOR_BYTES - 1u) / SECTOR_BYTES;
    uart_puts("[linux] loading raw Image to ");
    uart_hex(dest);
    uart_puts(" / bytes: ");
    uart_hex(size);
    uart_newline();
    unsigned char *dst = (unsigned char *)(uintptr_t)dest;
    for (uint64_t block = 0; block < blocks;) {
        uint64_t remain = blocks - block;
        uint32_t count = (uint32_t)(remain < 16u ? remain : 16u);
        if (arm64_virtio_blk_read_many(LinuxFirstSector + block,
                                       ReadBatch, count)) {
            uart_puts("[linux] disk read error, refusing boot\n");
            return -2;
        }
        uint64_t left = size - block * SECTOR_BYTES;
        size_t chunk = left < SECTOR_BYTES * count ? (size_t)left :
                       SECTOR_BYTES * count;
        for (size_t i = 0; i < chunk; ++i)
            dst[block * SECTOR_BYTES + i] = ReadBatch[i];
        block += count;
    }
    arm64_post_reserve_payload(dest, size);
    uart_puts("[linux] Image loaded; entering AArch64 Image entry\n");
    arm64_handoff((void *)(uintptr_t)dest, LinuxDtb);
}

void arm64_linux_discover(void *dtb)
{
    const uint64_t disk_sectors = arm64_virtio_blk_capacity();
    if (!disk_sectors || arm64_virtio_blk_read(0, DiskMBR))
        return;
    if (DiskMBR[510] != 0x55 || DiskMBR[511] != 0xaa)
        return;
    /* Select an explicitly active Linux MBR partition first, then the first
     * other 0x83 partition.  We do not parse extended/protective GPT entries. */
    int chosen = -1;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *part = DiskMBR + 446u + i * 16u;
        if (part[4] != LINUX_MBR_PARTITION_TYPE)
            continue;
        uint32_t first = u32le(part + 8u);
        uint32_t count = u32le(part + 12u);
        if (!first || !count || first >= disk_sectors ||
            count > disk_sectors - first)
            continue;
        if (chosen >= 0 && part[0] != 0x80)
            continue;
        chosen = (int)i;
        LinuxFirstSector = first;
        LinuxSectorCount = count;
        if (part[0] == 0x80)
            break;
    }
    if (chosen < 0) {
        if (arm64_gpt_find_linux(&LinuxFirstSector, &LinuxSectorCount))
            return;
        uart_puts("[gpt] validated Linux partition candidate\n");
    }
    if (arm64_virtio_blk_read(LinuxFirstSector, Sector))
        return;
    if (arm64_linux_image_parse(Sector, LinuxSectorCount, &LinuxHeader)) {
        uart_puts("[linux] partition found; unsupported Image header\n");
        return;
    }
    LinuxDtb = dtb;
    LinuxTarget.priority = 5;
    LinuxTarget.name = "raw Linux ARM64 Image on VirtIO block";
    LinuxTarget.attempt = boot_linux_image;
    LinuxTarget.context = NULL;
    arm64_boot_register(&LinuxTarget);
    uart_puts("[linux] supported ARM64 Image candidate at LBA ");
    uart_hex(LinuxFirstSector);
    uart_newline();
}
