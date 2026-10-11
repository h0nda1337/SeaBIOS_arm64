/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Native AArch64 MBR/FAT32 chainloader. No x86 VBR code and no EFI ABI. */
#include "include/arm64.h"
#include "include/bootorder.h"
#include "include/pe_legacy.h"
#include "include/fat32.h"
#include "../src/sha.h"

#define BPS 512u
#define LEGACY_BASE UINT64_C(0x49000000)
#define LEGACY_MAX (1024u * 1024u)
#define LEGACY_HEADER 64u
#define FAT32_MIN_CLUSTERS 65525u
#define FAT32_MAX_CLUSTERS 0x0fffffefu

struct legacy_candidate {
    struct arm64_boot_target target;
    uint32_t drive_index;
    struct fat32_volume volume;
    uint32_t file_cluster, file_bytes;
    void *dtb;
};
static struct legacy_candidate LegacyCandidates[ARM64_MAX_BLOCK_DRIVES];
static struct fat32_context Fs;
static void *BootDtb;
static uint8_t *const LegacyMemory = (uint8_t *)(uintptr_t)LEGACY_BASE;
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t fnv1a32(const uint8_t *p, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int load_legacy_stage(void *context)
{
    const struct legacy_candidate *candidate = context;
    if (!candidate)
        return -1;
    Fs.disk_index = candidate->drive_index;
    Fs.volume = candidate->volume;
    Fs.cache_lba = UINT64_MAX;
    BootDtb = candidate->dtb;
    uart_puts("[boot] legacy drive index ");
    uart_dec(Fs.disk_index); uart_newline();
    if (candidate->file_bytes < LEGACY_HEADER + 4u ||
        candidate->file_bytes > LEGACY_MAX ||
        !arm64_post_can_load(LEGACY_BASE, candidate->file_bytes)) return -1;
    uint8_t *dst = LegacyMemory;
    if (arm64_fat32_load(&Fs, candidate->file_cluster,
                         candidate->file_bytes, dst, LEGACY_MAX)) return -2;
    uint32_t FileBytes = candidate->file_bytes;
    if (dst[0] == 'M' && dst[1] == 'Z') {
        uint64_t entry = 0;
        uint32_t mapped = 0;
        if (arm64_pe_legacy_map(dst, FileBytes, &entry, &mapped)) {
            uart_puts("[legacy] PE32+ ARM64 image rejected\n");
            return -8;
        }
        arm64_cache_clean_range((void *)(uintptr_t)PE_LEGACY_BASE, mapped);
        arm64_post_reserve_payload(PE_LEGACY_BASE, mapped);
        uart_puts("[legacy] native PE32+ ARM64 stage2 -> handoff (no EFI)\n");
        arm64_handoff((void *)(uintptr_t)entry, BootDtb);
    }
    static const char magic[7] = { 'S','B','A','R','M','L','E' };
    for (unsigned i = 0; i < 7; ++i)
        if (dst[i] != (uint8_t)magic[i]) {
            uart_puts("[legacy] file is not a native ARM64 legacy stage2\n");
            return -6;
        }
    uint32_t length = rd32(dst + 8);
    if (!length || length > FileBytes - LEGACY_HEADER)
        return -7;
    if (dst[7] == '1') {
        if (fnv1a32(dst + LEGACY_HEADER, length) != rd32(dst + 12)) {
            uart_puts("[legacy] stage2 checksum/length rejected\n");
            return -7;
        }
    } else if (dst[7] == '2') {
        // This is an integrity check, NOT authentication or Secure Boot.
        uint8_t digest[32];
        sha256(dst + LEGACY_HEADER, length, digest);
        unsigned mismatch = 0;
        for (unsigned i = 0; i < 32; ++i)
            mismatch |= digest[i] ^ dst[16 + i];
        if (mismatch) {
            uart_puts("[legacy] stage2 SHA-256 mismatch\n");
            return -7;
        }
        uart_puts("[legacy] stage2 SHA-256 OK (not authenticated)\n");
    } else {
        uart_puts("[legacy] unknown stage2 format\n");
        return -6;
    }
    arm64_cache_clean_range(dst, FileBytes);
    arm64_post_reserve_payload(LEGACY_BASE, FileBytes);
    uart_puts("[legacy] MBR FAT32 ARM64 stage2 verified -> handoff\n");
    arm64_handoff(dst + LEGACY_HEADER, BootDtb);
}

void arm64_legacy_discover(void *dtb)
{
    for (uint32_t disk = 0; disk < arm64_virtio_blk_count() &&
                            disk < ARM64_MAX_BLOCK_DRIVES; ++disk) {
        if (arm64_fat32_probe_active(&Fs, disk)) continue;
        uint32_t boot = 0, ignored = 0;
        uint32_t FileCluster = 0, FileBytes = 0;
        if (arm64_fat32_find(&Fs, Fs.volume.root_cluster,
                             "BOOT       ", 1, &boot, &ignored)) continue;
        if (arm64_fat32_find(&Fs, boot, "ARM64   BIN", 0,
                             &FileCluster, &FileBytes) &&
            arm64_fat32_find(&Fs, boot, "ARM64   EXE", 0,
                             &FileCluster, &FileBytes)) continue;
        if (FileBytes < LEGACY_HEADER || FileBytes > LEGACY_MAX) {
            uart_puts("[legacy] stage2 size rejected\n");
            continue;
        }
        struct legacy_candidate *candidate = &LegacyCandidates[disk];
        candidate->drive_index = disk;
        candidate->volume = Fs.volume;
        candidate->file_cluster = FileCluster;
        candidate->file_bytes = FileBytes;
        candidate->dtb = dtb;
        candidate->target.name = "active MBR FAT32 native AArch64 stage2";
        candidate->target.priority = 3;
        candidate->target.attempt = load_legacy_stage;
        candidate->target.context = candidate;
        candidate->target.firmware_path = arm64_boot_disk_path(disk);
        arm64_boot_register(&candidate->target);
        uart_puts("[legacy] active MBR FAT32 ARM64.BIN/ARM64.EXE candidate, disk ");
        uart_dec(disk); uart_newline();
    }
}
