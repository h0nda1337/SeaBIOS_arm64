/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/linuximage.h"
#include "include/bootorder.h"
#include "include/gpt.h"
#include "include/fdt_boot.h"

/* A minimal direct Linux Image handoff from a raw MBR/GPT Linux partition.
 * This deliberately omits filesystems, EFI, initrd, DTB relocation and
 * cryptographic signature checks. Only QEMU virt has been tested. */
#define LINUX_BASE UINT64_C(0x48000000) /* 2 MiB aligned inside QEMU RAM */
#define LINUX_MBR_PARTITION_TYPE 0x83u
#define SECTOR_BYTES 512u
#define INITRD_MBR_PARTITION_TYPE 0xdau /* experimental raw data */
#define MAX_INITRD_BYTES (16u * 1024u * 1024u)
#define LINUX_CMDLINE "console=ttyAMA0,115200 earlycon=pl011,mmio32,0x09000000 rdinit=/init"
static uint8_t Sector[SECTOR_BYTES];
static uint8_t ReadBatch[SECTOR_BYTES * 16u];
static uint8_t DiskMBR[SECTOR_BYTES];
struct linux_candidate {
    struct arm64_boot_target target;
    uint32_t drive_index;
    uint64_t first_sector, sector_count;
    uint64_t initrd_sector, initrd_sector_count;
    struct arm64_linux_image header;
    void *dtb;
};
static struct linux_candidate LinuxCandidates[ARM64_MAX_BLOCK_DRIVES];

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int boot_linux_image(void *context)
{
    const struct linux_candidate *candidate = context;
    if (!candidate)
        return -1;
    const uint64_t dest = LINUX_BASE + candidate->header.text_offset;
    const uint64_t size = candidate->header.image_size;
    uart_puts("[boot] Linux drive index ");
    uart_dec(candidate->drive_index); uart_newline();
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
        if (arm64_virtio_blk_read_many_at(candidate->drive_index,
                                       candidate->first_sector + block,
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
    /* Kernel bytes written by the bootloader must reach PoC before execute. */
    arm64_cache_clean_range(dst, size);

    uint64_t initrd_base = 0, initrd_end = 0;
    if (candidate->initrd_sector_count) {
        const uint64_t nbytes = candidate->initrd_sector_count * SECTOR_BYTES;
        /* Round the placement AFTER the loaded kernel, preventing overlap
         * even when the Image carries a large text_offset or padding. */
        if (dest + size > UINT64_MAX - UINT64_C(0x1fffff))
            return -4;
        initrd_base = (dest + size + UINT64_C(0x1fffff)) &
                      ~UINT64_C(0x1fffff);
        if (!arm64_post_can_load(initrd_base, nbytes)) {
            uart_puts("[initrd] no safe RAM for raw initrd\n");
            return -5;
        }
        unsigned char *initrd = (unsigned char *)(uintptr_t)initrd_base;
        for (uint64_t block = 0; block < candidate->initrd_sector_count;) {
            uint32_t count = (uint32_t)(candidate->initrd_sector_count - block < 16u ?
                             candidate->initrd_sector_count - block : 16u);
            if (arm64_virtio_blk_read_many_at(candidate->drive_index,
                                           candidate->initrd_sector + block,
                                           ReadBatch, count)) {
                uart_puts("[initrd] raw disk read failed\n");
                return -6;
            }
            for (uint32_t i = 0; i < count * SECTOR_BYTES; ++i)
                initrd[block * SECTOR_BYTES + i] = ReadBatch[i];
            block += count;
        }
        arm64_cache_clean_range(initrd, nbytes);
        initrd_end = initrd_base + nbytes;
        uart_puts("[initrd] raw partition loaded at ");
        uart_hex(initrd_base);
        uart_puts(" / bytes ");
        uart_hex(nbytes);
        uart_newline();
    }
    const void *new_dtb = NULL;
    uint32_t dtb_size = 0;
    int status = arm64_fdt_prepare(candidate->dtb, LINUX_CMDLINE,
                                   initrd_base, initrd_end,
                                   &new_dtb, &dtb_size);
    if (status || !new_dtb) {
        uart_puts("[fdt] failed to prepare /chosen, refusing boot\n");
        return -7;
    }
    arm64_cache_clean_range(new_dtb, dtb_size);
    uart_puts("[fdt] prepared /chosen for Linux: ");
    uart_hex(dtb_size);
    uart_newline();
    /* Reserve only after every I/O and DTB operation succeeds. Otherwise a
     * failed first candidate would prevent booting another disk at the same
     * kernel load address, even though no handoff occurred. */
    arm64_post_reserve_payload(dest, size);
    if (initrd_end)
        arm64_post_reserve_payload(initrd_base, initrd_end - initrd_base);
    uart_puts("[linux] Image loaded; entering AArch64 Image entry\n");
    arm64_handoff((void *)(uintptr_t)dest, (void *)new_dtb);
}

void arm64_linux_discover(void *dtb)
{
    for (uint32_t disk = 0; disk < arm64_virtio_blk_count() &&
                            disk < ARM64_MAX_BLOCK_DRIVES; ++disk) {
        const uint64_t disk_sectors = arm64_virtio_blk_capacity_at(disk);
        if (!disk_sectors || arm64_virtio_blk_read_at(disk, 0, DiskMBR))
            continue;
        if (DiskMBR[510] != 0x55 || DiskMBR[511] != 0xaa)
            continue;
        uint64_t first_sector = 0, sector_count = 0;
        struct arm64_linux_image header;
        /* Raw initrd format for this experimental loader: an OPTIONAL MBR
         * partition of type 0xda. This is NOT a filesystem parser and should not
         * be confused with any standard distro boot protocol. */
        uint64_t initrd_sector = 0, initrd_count = 0;
        int bad_initrd = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *part = DiskMBR + 446u + i * 16u;
            if (part[4] != INITRD_MBR_PARTITION_TYPE) continue;
            uint64_t first = u32le(part + 8u);
            uint64_t count = u32le(part + 12u);
            if (first && count && first < disk_sectors &&
                count <= disk_sectors - first &&
                count <= MAX_INITRD_BYTES / SECTOR_BYTES) {
                initrd_sector = first;
                initrd_count = count;
                break;
            }
            /* Malformed configured initrd: refuse the candidate instead of
             * silently booting without the caller-requested initrd. */
            uart_puts("[initrd] invalid raw initrd partition\n");
            bad_initrd = 1;
            break;
        }
        if (bad_initrd)
            continue;
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
            first_sector = first;
            sector_count = count;
            if (part[0] == 0x80)
                break;
        }
        if (chosen < 0) {
            if (arm64_gpt_find_linux_at(disk, &first_sector, &sector_count))
                continue;
            uart_puts("[gpt] validated Linux partition candidate\n");
        }
        if (initrd_count &&
            initrd_sector < first_sector + sector_count &&
            first_sector < initrd_sector + initrd_count) {
            uart_puts("[initrd] overlaps raw kernel partition; refusing boot\n");
            continue;
        }
        if (arm64_virtio_blk_read_at(disk, first_sector, Sector))
            continue;
        if (arm64_linux_image_parse(Sector, sector_count, &header)) {
            uart_puts("[linux] partition found; unsupported Image header\n");
            continue;
        }

        struct linux_candidate *candidate = &LinuxCandidates[disk];
        candidate->drive_index = disk;
        candidate->first_sector = first_sector;
        candidate->sector_count = sector_count;
        candidate->initrd_sector = initrd_sector;
        candidate->initrd_sector_count = initrd_count;
        candidate->header = header;
        candidate->dtb = dtb;
        candidate->target.priority = 5;
        candidate->target.name = "raw Linux ARM64 Image on VirtIO block";
        candidate->target.attempt = boot_linux_image;
        candidate->target.context = candidate;
        candidate->target.firmware_path = arm64_boot_disk_path(disk);
        arm64_boot_register(&candidate->target);
        uart_puts("[linux] supported ARM64 Image candidate at LBA ");
        uart_hex(first_sector);
        uart_puts(" on disk ");
        uart_dec(disk); uart_newline();
    }
}
