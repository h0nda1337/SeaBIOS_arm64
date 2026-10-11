/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Shared read-only FAT32 loader for native ARM64 MBR boot targets.
 * No VBR execution, no EFI and no write support. */
#include "include/fat32.h"
#include "include/arm64.h"

#define FAT32_MIN_CLUSTERS 65525u
#define FAT32_MAX_CLUSTERS 0x0fffffefu
#define BPS FAT32_SECTOR_SIZE

static uint16_t rd16(const uint8_t *p)
{ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int cluster_sector(const struct fat32_context *c, uint32_t cluster,
                          uint64_t *sector)
{
    if (cluster < 2 || cluster - 2 >= c->volume.clusters)
        return -1;
    *sector = c->volume.data_lba + (uint64_t)(cluster - 2) * c->volume.spc;
    return 0;
}
static int nextcluster(struct fat32_context *c, uint32_t cluster, uint32_t *next)
{
    uint64_t ignored;
    if (cluster_sector(c, cluster, &ignored)) return -1;
    const uint64_t offset = (uint64_t)cluster * 4u;
    if (offset / BPS >= c->volume.fat_sectors) return -1;
    const uint64_t lba = c->volume.fat_lba + offset / BPS;
    if (lba != c->cache_lba) {
        if (arm64_virtio_blk_read_at(c->disk_index, lba, c->fat_sector))
            return -1;
        c->cache_lba = lba;
    }
    *next = rd32(c->fat_sector + offset % BPS) & 0x0fffffffu;
    if (*next == 0x0ffffff7u ||
        (*next >= 0x0ffffff0u && *next < 0x0ffffff8u))
        return -1;
    return 0;
}

int arm64_fat32_probe_active(struct fat32_context *c, uint32_t disk)
{
    if (!c) return -1;
    c->disk_index = disk;
    c->cache_lba = UINT64_MAX;
    const uint64_t sectors = arm64_virtio_blk_capacity_at(disk);
    if (sectors < 2048 || arm64_virtio_blk_read_at(disk, 0, c->sector))
        return -1;
    if (c->sector[510] != 0x55 || c->sector[511] != 0xaa) return -1;
    uint64_t start = 0, count = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *e = c->sector + 446 + i * 16;
        if (e[0] != 0x80 || (e[4] != 0x0b && e[4] != 0x0c)) continue;
        start = rd32(e + 8); count = rd32(e + 12);
        if (start && count && start < sectors && count <= sectors - start)
            break;
        return -1;
    }
    if (!start || !count) return -1;
    c->volume.part_lba = start;
    if (arm64_virtio_blk_read_at(disk, start, c->sector)) return -1;
    const uint8_t *p = c->sector;
    if (p[510] != 0x55 || p[511] != 0xaa) return -1;
    const uint32_t spc = p[13], reserved = rd16(p + 14);
    const uint32_t fats = p[16], fat_sectors = rd32(p + 36);
    const uint32_t total = rd32(p + 32), root = rd32(p + 44);
    const uint16_t flags = rd16(p + 40);
    if (rd16(p + 11) != BPS || !spc || (spc & (spc - 1)) || spc > 64 ||
        !reserved || !fats || fats > 2 || !fat_sectors || !total ||
        total > count || rd16(p + 17) || rd16(p + 22) || rd16(p + 42))
        return -1;
    const uint64_t overhead = (uint64_t)reserved + (uint64_t)fats * fat_sectors;
    if (overhead >= total || (flags & 0x7f70u)) return -1;
    if ((flags & 0x80u) && (flags & 0x0fu) >= fats) return -1;
    const uint64_t clusters = (total - overhead) / spc;
    if (clusters < FAT32_MIN_CLUSTERS || clusters > FAT32_MAX_CLUSTERS ||
        root < 2 || root - 2 >= clusters ||
        ((clusters + 2u) * 4u + BPS - 1u) / BPS > fat_sectors)
        return -1;
    c->volume.spc = spc;
    c->volume.fat_sectors = fat_sectors;
    c->volume.clusters = clusters;
    c->volume.root_cluster = root;
    c->volume.fat_lba = start + reserved +
        ((flags & 0x80u) ? (uint64_t)(flags & 0x0fu) * fat_sectors : 0);
    c->volume.data_lba = start + overhead;
    return 0;
}

int arm64_fat32_find(struct fat32_context *c, uint32_t first,
                      const char name[11], int isdir,
                      uint32_t *cluster_out, uint32_t *size_out)
{
    if (!c || !name || !cluster_out || !size_out) return -1;
    uint32_t cluster = first;
    for (uint32_t visited = 0; visited < c->volume.clusters; ++visited) {
        uint64_t sector;
        if (cluster_sector(c, cluster, &sector)) return -1;
        for (uint32_t s = 0; s < c->volume.spc; ++s) {
            if (arm64_virtio_blk_read_at(c->disk_index, sector + s,
                                         c->sector)) return -1;
            for (uint32_t p = 0; p < BPS; p += 32u) {
                const uint8_t *e = c->sector + p;
                if (e[0] == 0) return -2;
                if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & 0x08))
                    continue;
                unsigned match = 1;
                for (unsigned i = 0; i < 11; ++i)
                    if (e[i] != (uint8_t)name[i]) match = 0;
                if (match && !!(e[11] & 0x10) == !!isdir) {
                    *cluster_out = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
                    *size_out = rd32(e + 28);
                    return cluster_sector(c, *cluster_out, &sector);
                }
            }
        }
        uint32_t next;
        if (nextcluster(c, cluster, &next)) return -1;
        if (next >= 0x0ffffff8u) return -2;
        cluster = next;
    }
    return -1;
}

int arm64_fat32_first_sector(struct fat32_context *c, uint32_t cluster,
                             uint8_t sector[BPS])
{
    uint64_t lba;
    if (!c || !sector || cluster_sector(c, cluster, &lba)) return -1;
    return arm64_virtio_blk_read_at(c->disk_index, lba, sector);
}

int arm64_fat32_load(struct fat32_context *c, uint32_t cluster,
                     uint32_t bytes, void *destination, uint32_t capacity)
{
    if (!c || !destination || !bytes || bytes > capacity) return -1;
    uint8_t *dst = destination;
    uint32_t remaining = bytes, copied = 0;
    for (uint32_t visited = 0; remaining && visited < c->volume.clusters;
         ++visited) {
        uint64_t sector;
        if (cluster_sector(c, cluster, &sector)) return -2;
        for (uint32_t s = 0; s < c->volume.spc && remaining; ++s) {
            if (arm64_virtio_blk_read_at(c->disk_index, sector + s,
                                         c->sector)) return -3;
            uint32_t take = remaining < BPS ? remaining : BPS;
            for (uint32_t i = 0; i < take; ++i) dst[copied + i] = c->sector[i];
            remaining -= take;
            copied += take;
        }
        if (!remaining) break;
        uint32_t next;
        if (nextcluster(c, cluster, &next) || next >= 0x0ffffff8u)
            return -4;
        cluster = next;
    }
    return remaining ? -5 : 0;
}
