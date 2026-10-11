/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Native AArch64 MBR/FAT32 chainloader. No x86 VBR code and no EFI ABI. */
#include "include/arm64.h"
#include "include/bootorder.h"
#include "include/pe_legacy.h"
#include "../src/sha.h"

#define BPS 512u
#define LEGACY_BASE UINT64_C(0x49000000)
#define LEGACY_MAX (1024u * 1024u)
#define LEGACY_HEADER 64u
#define FAT32_MIN_CLUSTERS 65525u
#define FAT32_MAX_CLUSTERS 0x0fffffefu

struct fat32_volume {
    uint64_t part_lba, fat_lba, data_lba;
    uint32_t fat_sectors, root_cluster, clusters;
    uint8_t spc;
};

static struct fat32_volume Volume;
struct legacy_candidate {
    struct arm64_boot_target target;
    uint32_t drive_index;
    struct fat32_volume volume;
    uint32_t file_cluster, file_bytes;
    void *dtb;
};
static struct legacy_candidate LegacyCandidates[ARM64_MAX_BLOCK_DRIVES];
static uint32_t DriveIndex;
static void *BootDtb;
static uint8_t Sector[BPS];
static uint8_t FatSector[BPS];
static uint8_t Directory[BPS];
static uint64_t FatSectorLba = UINT64_MAX;
static uint32_t FileCluster, FileBytes;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int readpart(uint64_t offset, uint8_t *data)
{
    return arm64_virtio_blk_read_at(DriveIndex, Volume.part_lba + offset, data);
}

/* FAT entries are cached a sector at a time; never trust a cluster value from
 * untrusted disk data before using it as an address. */
static int nextcluster(uint32_t cluster, uint32_t *next)
{
    if (cluster < 2 || cluster - 2 >= Volume.clusters)
        return -1;
    uint64_t fatbyte = (uint64_t)cluster * 4u;
    uint64_t sector = Volume.fat_lba + fatbyte / BPS;
    if (fatbyte / BPS >= Volume.fat_sectors)
        return -1;
    if (sector != FatSectorLba) {
        if (arm64_virtio_blk_read_at(DriveIndex, sector, FatSector))
            return -1;
        FatSectorLba = sector;
    }
    *next = rd32(FatSector + (fatbyte % BPS)) & 0x0fffffffu;
    if (*next == 0x0ffffff7u || (*next >= 0x0ffffff0u && *next < 0x0ffffff8u))
        return -1;
    return 0;
}

static int cluster_sector(uint32_t cluster, uint64_t *sector)
{
    if (cluster < 2 || cluster - 2 >= Volume.clusters)
        return -1;
    *sector = Volume.data_lba + (uint64_t)(cluster - 2) * Volume.spc;
    return 0;
}

/* Walk bounded FAT32 directories using exact 8.3 names. LFN entries are
 * deliberately ignored, but the bounds do not depend on directory contents. */
static int findentry(uint32_t first, const char name[11], int isdir,
                     uint32_t *cluster_out, uint32_t *size_out)
{
    uint32_t cluster = first;
    for (uint32_t visited = 0; visited < Volume.clusters; ++visited) {
        uint64_t sector;
        if (cluster_sector(cluster, &sector)) return -1;
        for (uint32_t s = 0; s < Volume.spc; ++s) {
            if (arm64_virtio_blk_read_at(DriveIndex, sector + s, Directory)) return -1;
            for (uint32_t p = 0; p < BPS; p += 32u) {
                const uint8_t *e = Directory + p;
                if (e[0] == 0) return -2;
                if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & 0x08))
                    continue;
                int match = 1;
                for (unsigned i = 0; i < 11; ++i)
                    if (e[i] != (uint8_t)name[i]) match = 0;
                if (match && !!(e[11] & 0x10) == !!isdir) {
                    *cluster_out = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
                    *size_out = rd32(e + 28);
                    return cluster_sector(*cluster_out, &sector);
                }
            }
        }
        uint32_t next;
        if (nextcluster(cluster, &next)) return -1;
        if (next >= 0x0ffffff8u) return -2;
        cluster = next;
    }
    return -1; /* cyclic or unbounded directory */
}

/* Parse a genuinely active MBR FAT32 volume, not an x86 boot-sector ABI. */
static int legacy_probe_mbr(void)
{
    uint64_t sectors = arm64_virtio_blk_capacity_at(DriveIndex);
    if (sectors < 2048 || arm64_virtio_blk_read_at(DriveIndex, 0, Sector)) return -1;
    if (Sector[510] != 0x55 || Sector[511] != 0xaa) return -1;
    uint64_t start = 0, count = 0;
    for (int i = 0; i < 4; ++i) {
        const uint8_t *e = Sector + 446 + i * 16;
        if (e[0] != 0x80 || (e[4] != 0x0b && e[4] != 0x0c)) continue;
        start = rd32(e + 8); count = rd32(e + 12);
        if (start >= 1 && count && start < sectors && count <= sectors - start)
            break;
        return -1;
    }
    if (!start || !count) return -1;
    Volume.part_lba = start;
    if (readpart(0, Sector)) return -1;
    if (Sector[510] != 0x55 || Sector[511] != 0xaa) return -1;
    uint32_t spc = Sector[13], reserved = rd16(Sector + 14);
    uint32_t fats = Sector[16], fat_sectors = rd32(Sector + 36);
    uint32_t total = rd32(Sector + 32), root = rd32(Sector + 44);
    uint16_t fat_flags = rd16(Sector + 40);
    if (rd16(Sector + 11) != BPS || !spc || (spc & (spc - 1)) || spc > 64 ||
        !reserved || !fats || fats > 2 || !fat_sectors || !total ||
        total > count || rd16(Sector + 17) || rd16(Sector + 22) ||
        rd16(Sector + 42))
        return -1;
    uint64_t overhead = (uint64_t)reserved + (uint64_t)fats * fat_sectors;
    if (overhead >= total || (fat_flags & 0x7f70u)) return -1;
    if ((fat_flags & 0x80u) && (fat_flags & 0x0fu) >= fats) return -1;
    uint64_t clusters = (total - overhead) / spc;
    if (clusters < FAT32_MIN_CLUSTERS || clusters > FAT32_MAX_CLUSTERS ||
        root < 2 || root - 2 >= clusters ||
        ((clusters + 2u) * 4u + BPS - 1u) / BPS > fat_sectors)
        return -1;
    Volume.spc = (uint8_t)spc;
    Volume.fat_sectors = fat_sectors;
    Volume.clusters = (uint32_t)clusters;
    Volume.root_cluster = root;
    Volume.fat_lba = start + reserved +
        ((fat_flags & 0x80u) ? (uint64_t)(fat_flags & 0x0fu) * fat_sectors : 0);
    Volume.data_lba = start + overhead;
    FatSectorLba = UINT64_MAX;
    return 0;
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
    DriveIndex = candidate->drive_index;
    Volume = candidate->volume;
    FileCluster = candidate->file_cluster;
    FileBytes = candidate->file_bytes;
    BootDtb = candidate->dtb;
    FatSectorLba = UINT64_MAX; /* Never reuse the cache across devices. */
    uart_puts("[boot] legacy drive index ");
    uart_dec(DriveIndex); uart_newline();
    if (FileBytes < LEGACY_HEADER + 4u || FileBytes > LEGACY_MAX ||
        !arm64_post_can_load(LEGACY_BASE, FileBytes)) return -1;
    uint32_t remaining = FileBytes;
    uint32_t cluster = FileCluster;
    uint8_t *dst = (uint8_t *)(uintptr_t)LEGACY_BASE;
    uint32_t copied = 0;
    for (uint32_t visited = 0; remaining && visited < Volume.clusters; ++visited) {
        uint64_t sector;
        if (cluster_sector(cluster, &sector)) return -2;
        for (uint32_t s = 0; s < Volume.spc && remaining; ++s) {
            if (arm64_virtio_blk_read_at(DriveIndex, sector + s, Sector)) return -3;
            uint32_t take = remaining < BPS ? remaining : BPS;
            for (uint32_t i = 0; i < take; ++i) dst[copied + i] = Sector[i];
            remaining -= take;
            copied += take;
        }
        if (!remaining) break;
        uint32_t next;
        if (nextcluster(cluster, &next) || next >= 0x0ffffff8u) return -4;
        cluster = next;
    }
    if (remaining) return -5;
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
        DriveIndex = disk;
        FatSectorLba = UINT64_MAX;
        if (legacy_probe_mbr())
            continue;
        uint32_t boot = 0, ignored = 0;
        if (findentry(Volume.root_cluster, "BOOT       ", 1,
                      &boot, &ignored))
            continue;
        if (findentry(boot, "ARM64   BIN", 0, &FileCluster, &FileBytes) &&
            findentry(boot, "ARM64   EXE", 0, &FileCluster, &FileBytes))
            continue;
        if (FileBytes < LEGACY_HEADER || FileBytes > LEGACY_MAX) {
            uart_puts("[legacy] stage2 size rejected\n");
            continue;
        }
        struct legacy_candidate *candidate = &LegacyCandidates[disk];
        candidate->drive_index = disk;
        candidate->volume = Volume;
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
