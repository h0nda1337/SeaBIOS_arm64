/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef SEABIOS_ARM64_FAT32_H
#define SEABIOS_ARM64_FAT32_H
#include <stdint.h>

#define FAT32_SECTOR_SIZE 512u
struct fat32_volume {
    uint64_t part_lba, fat_lba, data_lba;
    uint32_t fat_sectors, root_cluster, clusters;
    uint8_t spc;
};

/* Mutable per-operation cache; never reuse it across physical drives. */
struct fat32_context {
    struct fat32_volume volume;
    uint32_t disk_index;
    uint64_t cache_lba;
    uint8_t fat_sector[FAT32_SECTOR_SIZE];
    uint8_t sector[FAT32_SECTOR_SIZE];
};

int arm64_fat32_probe_active(struct fat32_context *ctx, uint32_t disk);
int arm64_fat32_find(struct fat32_context *ctx, uint32_t directory,
                     const char name[11], int isdir,
                     uint32_t *cluster, uint32_t *bytes);
int arm64_fat32_first_sector(struct fat32_context *ctx, uint32_t cluster,
                             uint8_t sector[FAT32_SECTOR_SIZE]);
/* Loads the exact directory file size, following bounded FAT32 chains. */
int arm64_fat32_load(struct fat32_context *ctx, uint32_t cluster,
                     uint32_t bytes, void *destination, uint32_t capacity);
#endif
