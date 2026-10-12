/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/linuximage.h"
#include "include/bootorder.h"
#include "include/gpt.h"
#include "include/fdt_boot.h"
#include "include/fat32.h"
#include "../src/romfile.h"
#include "../src/e820map.h"

/* Direct Linux Image handoff from raw MBR/GPT or MBR/FAT32. Firmware loads
 * the Image and optional initrd; Linux owns the root filesystem. Only QEMU
 * virt has been tested. This native path has no EFI services. */
#define LINUX_BASE UINT64_C(0x48000000) /* 2 MiB aligned inside QEMU RAM */
#define LINUX_MBR_PARTITION_TYPE 0x83u
#define SECTOR_BYTES 512u
#define INITRD_MBR_PARTITION_TYPE 0xdau /* experimental raw data */
#define MAX_INITRD_BYTES (16u * 1024u * 1024u)
#define LINUX_CMDLINE "console=ttyAMA0,115200 earlycon=pl011,mmio32,0x09000000 rdinit=/init"
static char LinuxCmdline[ARM64_FDT_BOOTARG_MAX + 1u];
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
    /* Optional filesystem-backed Linux boot candidate (MBR FAT32). */
    uint8_t from_fat32;
    struct fat32_volume fat_volume;
    uint32_t kernel_cluster, kernel_file_bytes;
    uint32_t initrd_cluster, initrd_file_bytes;
};
static struct linux_candidate LinuxCandidates[ARM64_MAX_BLOCK_DRIVES];
static struct linux_candidate FatCandidates[ARM64_MAX_BLOCK_DRIVES];
/* Only one boot attempt runs at once; this FAT cache is reset per candidate. */
static struct fat32_context LinuxFat;
#define LINUX_FAT_IMAGE_NAME "IMAGE      "
#define LINUX_FAT_INITRD_NAME "INITRD  IMG"

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Reuse the SeaBIOS romfile registry for board/operator configuration.
 * A configured but malformed command line must not silently boot with the
 * diagnostic rdinit default. Read directly into bounded firmware storage. */
static const char *linux_cmdline(void)
{
    struct romfile_s *file = romfile_find("etc/linux-cmdline");
    if (!file)
        return LINUX_CMDLINE;
    if (!file->size || file->size > ARM64_FDT_BOOTARG_MAX ||
        file->copy(file, LinuxCmdline, ARM64_FDT_BOOTARG_MAX) !=
        (int)file->size)
        return NULL;
    uint32_t size = file->size;
    /* Permit a text-file newline or one trailing NUL from a fw_cfg string. */
    while (size && (LinuxCmdline[size - 1u] == '\n' ||
                   LinuxCmdline[size - 1u] == '\r' ||
                   LinuxCmdline[size - 1u] == '\0'))
        --size;
    if (!size)
        return NULL;
    for (uint32_t i = 0; i < size; ++i)
        if ((unsigned char)LinuxCmdline[i] < 0x20u ||
            (unsigned char)LinuxCmdline[i] > 0x7eu)
            return NULL;
    LinuxCmdline[size] = 0;
    uart_puts("[linux] command line from etc/linux-cmdline: ");
    uart_puts(LinuxCmdline);
    uart_newline();
    return LinuxCmdline;
}

static uint64_t candidate_initrd_bytes(const struct linux_candidate *candidate)
{
    return candidate->from_fat32 ? candidate->initrd_file_bytes :
           candidate->initrd_sector_count * SECTOR_BYTES;
}

/* Retain the established load address when it fits. On smaller RAM machines,
 * use the original SeaBIOS E820 map to select another 2 MiB aligned base.
 * Check both payloads before writing either; never allocate through firmware
 * state, the source DTB, or a device-tree reservation. */
static uint64_t linux_region(const struct linux_candidate *candidate,
                             uint64_t base)
{
    uint64_t offset = candidate->header.text_offset;
    uint64_t size = candidate->header.image_size;
    if (base > UINT64_MAX - offset)
        return 0;
    uint64_t dest = base + offset;
    if (!arm64_post_can_load(dest, size))
        return 0;
    uint64_t initbytes = candidate_initrd_bytes(candidate);
    if (initbytes) {
        if (dest + size > UINT64_MAX - UINT64_C(0x1fffff))
            return 0;
        uint64_t initbase = (dest + size + UINT64_C(0x1fffff)) &
                            ~UINT64_C(0x1fffff);
        if (!arm64_post_can_load(initbase, initbytes))
            return 0;
    }
    return dest;
}

static uint64_t linux_load_address(const struct linux_candidate *candidate)
{
    uint64_t dest = linux_region(candidate, LINUX_BASE);
    if (dest)
        return dest;
    for (int i = 0; i < e820_count; ++i) {
        const struct e820entry *region = &e820_list[i];
        if (region->type != E820_RAM ||
            region->start > UINT64_MAX - UINT64_C(0x1fffff))
            continue;
        uint64_t base = (region->start + UINT64_C(0x1fffff)) &
                        ~UINT64_C(0x1fffff);
        dest = linux_region(candidate, base);
        if (dest) {
            uart_puts("[linux] selected safe RAM base from SeaBIOS E820: ");
            uart_hex(base);
            uart_newline();
            return dest;
        }
    }
    return 0;
}

static int boot_linux_image(void *context)
{
    const struct linux_candidate *candidate = context;
    if (!candidate)
        return -1;
    const char *cmdline = linux_cmdline();
    if (!cmdline) {
        uart_puts("[linux] invalid etc/linux-cmdline; refusing boot\n");
        return -8;
    }
    const uint64_t dest = linux_load_address(candidate);
    const uint64_t size = candidate->header.image_size;
    uart_puts("[boot] Linux drive index ");
    uart_dec(candidate->drive_index); uart_newline();
    /* These regions are checked against actual RAM and firmware reservations,
     * including the boot-time stack, DTB and QEMU flash mappings. */
    if (!dest) {
        uart_puts("[linux] no safe RAM region for kernel and initrd\n");
        return -1;
    }
    const uint64_t blocks = (size + SECTOR_BYTES - 1u) / SECTOR_BYTES;
    uart_puts("[linux] loading raw Image to ");
    uart_hex(dest);
    uart_puts(" / bytes: ");
    uart_hex(size);
    uart_newline();
    unsigned char *dst = (unsigned char *)(uintptr_t)dest;
    if (candidate->from_fat32) {
        LinuxFat.volume = candidate->fat_volume;
        LinuxFat.disk_index = candidate->drive_index;
        LinuxFat.cache_lba = UINT64_MAX;
        if (arm64_fat32_load(&LinuxFat, candidate->kernel_cluster,
                             candidate->kernel_file_bytes, dst,
                             (uint32_t)size)) {
            uart_puts("[linux] FAT32 kernel chain invalid or truncated\n");
            return -2;
        }
        /* The Linux Image header may include a BSS tail in image_size.
         * This is memory-backed and zero-filled, not disk-backed. */
        for (uint64_t i = candidate->kernel_file_bytes; i < size; ++i)
            dst[i] = 0;
        uart_puts("[linux] FAT32 /BOOT/IMAGE loaded on disk ");
        uart_dec(candidate->drive_index); uart_newline();
    } else {
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
    }
    /* Kernel bytes written by the bootloader must reach PoC before execute. */
    arm64_cache_clean_range(dst, size);

    uint64_t initrd_base = 0, initrd_end = 0;
    if (candidate->initrd_sector_count || candidate->initrd_file_bytes) {
        uint64_t nbytes = candidate_initrd_bytes(candidate);
        if (dest + size > UINT64_MAX - UINT64_C(0x1fffff))
            return -4;
        initrd_base = (dest + size + UINT64_C(0x1fffff)) &
                      ~UINT64_C(0x1fffff);
        if (!arm64_post_can_load(initrd_base, nbytes)) {
            uart_puts("[initrd] no safe RAM for initrd\n");
            return -5;
        }
        unsigned char *initrd = (unsigned char *)(uintptr_t)initrd_base;
        if (candidate->from_fat32) {
            if (arm64_fat32_load(&LinuxFat, candidate->initrd_cluster,
                                 candidate->initrd_file_bytes, initrd,
                                 (uint32_t)nbytes)) {
                uart_puts("[initrd] FAT32 chain invalid\n");
                return -6;
            }
        } else {
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
        }
        arm64_cache_clean_range(initrd, nbytes);
        initrd_end = initrd_base + nbytes;
        uart_puts(candidate->from_fat32 ? "[initrd] loaded at " :
                  "[initrd] raw partition loaded at ");
        uart_hex(initrd_base);
        uart_puts(" / bytes ");
        uart_hex(nbytes);
        uart_newline();
    }
    const void *new_dtb = NULL;
    uint32_t dtb_size = 0;
    int status = arm64_fdt_prepare(candidate->dtb, cmdline,
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

/* Independent boot candidates: an active FAT32 MBR volume containing
 * /BOOT/IMAGE and optional /BOOT/INITRD.IMG. The x86 MBR/VBR code is not
 * executed: this is the native AArch64 counterpart using SeaBIOS disk_op_s. */
static void arm64_linux_fat_discover(void *dtb, uint32_t disk)
{
    if (arm64_fat32_probe_active(&LinuxFat, disk)) return;
    uint32_t boot = 0, ignored = 0;
    if (arm64_fat32_find(&LinuxFat, LinuxFat.volume.root_cluster,
                         "BOOT       ", 1, &boot, &ignored)) return;
    uint32_t cluster = 0, file_bytes = 0;
    if (arm64_fat32_find(&LinuxFat, boot, LINUX_FAT_IMAGE_NAME, 0,
                         &cluster, &file_bytes)) return;
    if (file_bytes < SECTOR_BYTES || file_bytes > 64u * 1024u * 1024u ||
        arm64_fat32_first_sector(&LinuxFat, cluster, Sector)) return;
    struct arm64_linux_image header;
    /* Allow a file smaller than image_size only when it has a valid Image
     * header; the remaining memory is zero-filled before handoff. */
    if (arm64_linux_image_parse(Sector, 64u * 1024u * 1024u / SECTOR_BYTES,
                                &header) || header.image_size < file_bytes)
        return;
    uint32_t initrd_cluster = 0, initrd_bytes = 0;
    int initrd_status = arm64_fat32_find(&LinuxFat, boot,
                                        LINUX_FAT_INITRD_NAME, 0,
                                        &initrd_cluster, &initrd_bytes);
    if (!initrd_status) {
        if (!initrd_bytes || initrd_bytes > MAX_INITRD_BYTES) {
            uart_puts("[linux] FAT32 initrd size rejected\n");
            return;
        }
    } else if (initrd_status == -2) {
        initrd_cluster = 0;
        initrd_bytes = 0;
    } else {
        uart_puts("[linux] FAT32 initrd lookup failed; refusing candidate\n");
        return;
    }
    struct linux_candidate *candidate = &FatCandidates[disk];
    candidate->drive_index = disk;
    candidate->from_fat32 = 1;
    candidate->fat_volume = LinuxFat.volume;
    candidate->kernel_cluster = cluster;
    candidate->kernel_file_bytes = file_bytes;
    candidate->initrd_cluster = initrd_cluster;
    candidate->initrd_file_bytes = initrd_bytes;
    candidate->header = header;
    candidate->dtb = dtb;
    candidate->target.priority = 5;
    candidate->target.name = "FAT32 /BOOT/IMAGE Linux AArch64 on VirtIO";
    candidate->target.attempt = boot_linux_image;
    candidate->target.context = candidate;
    candidate->target.firmware_path = arm64_boot_disk_path(disk);
    arm64_boot_register(&candidate->target);
    uart_puts("[linux] FAT32 /BOOT/IMAGE candidate on disk ");
    uart_dec(disk); uart_newline();
}

void arm64_linux_discover(void *dtb)
{
    for (uint32_t disk = 0; disk < arm64_virtio_blk_count() &&
                            disk < ARM64_MAX_BLOCK_DRIVES; ++disk)
        arm64_linux_fat_discover(dtb, disk);
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
