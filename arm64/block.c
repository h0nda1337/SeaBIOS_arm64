/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* AArch64 storage registration and controller backend. The original
 * src/block.c owns drive mapping and process_op() on both architectures. */
#include "include/arm64.h"
#include "../src/block.h"
#include "../src/std/disk.h"

/* Private, bounded transport requests; clients use disk_op_s exclusively. */
extern int arm64_virtio_blk_read_many_transport_at(uint32_t index,
                           uint64_t lba, void *buf, uint32_t count);

static struct drive_s VirtioDrives[ARM64_MAX_BLOCK_DRIVES];
static uint8_t DriveRegistered[ARM64_MAX_BLOCK_DRIVES];
#define TRANSPORT_SECTORS 16u

struct drive_s *
arm64_block_drive_at(uint32_t index)
{
    if (index >= ARM64_MAX_BLOCK_DRIVES || !DriveRegistered[index] ||
        !arm64_virtio_blk_capacity_at(index))
        return NULL;
    return &VirtioDrives[index];
}

void
arm64_block_setup(void)
{
    for (uint32_t index = 0; index < arm64_virtio_blk_count(); index++) {
        uint64_t sectors = arm64_virtio_blk_capacity_at(index);
        if (index >= ARM64_MAX_BLOCK_DRIVES || !sectors || DriveRegistered[index])
            continue;
        struct drive_s *drive = &VirtioDrives[index];
        drive->type = DTYPE_VIRTIO_BLK;
        drive->blksize = DISK_SECTOR_SIZE;
        drive->sectors = sectors;
        drive->cntl_id = index;
        drive->max_segment_size = TRANSPORT_SECTORS * DISK_SECTOR_SIZE;
        drive->max_segments = 1;
        map_hd_drive(drive);
        DriveRegistered[index] = 1;
        uart_puts("[block] SeaBIOS drive_s registered (VirtIO-MMIO) index ");
        uart_dec(index); uart_newline();
        uart_puts("[block] shared SeaBIOS LCHS ");
        uart_dec(drive->lchs.cylinder); uart_putc('/');
        uart_dec(drive->lchs.head); uart_putc('/');
        uart_dec(drive->lchs.sector); uart_newline();
    }
}

/* Called by the original src/block.c flat-mode dispatcher. Chunk requests
 * at the controller limit while preserving SeaBIOS partial completion. */
int
arm64_block_process_op(struct disk_op_s *op)
{
    uint32_t index;
    for (index = 0; index < ARM64_MAX_BLOCK_DRIVES; index++)
        if (op->drive_fl == arm64_block_drive_at(index))
            break;
    if (index == ARM64_MAX_BLOCK_DRIVES)
        return DISK_RET_ENOTREADY;
    if (op->command == CMD_WRITE)
        return DISK_RET_EWRITEPROTECT;
    if (op->command != CMD_READ)
        return default_process_op(op);
    if (!op->count)
        return DISK_RET_EPARAM;
    if (op->lba >= op->drive_fl->sectors ||
        op->count > op->drive_fl->sectors - op->lba)
        return DISK_RET_EADDRNOTFOUND;

    u16 requested = op->count, completed = 0;
    while (completed < requested) {
        u16 count = requested - completed;
        if (count > TRANSPORT_SECTORS)
            count = TRANSPORT_SECTORS;
        if (arm64_virtio_blk_read_many_transport_at(
                index, op->lba + completed,
                (u8 *)op->buf_fl + (u32)completed * DISK_SECTOR_SIZE, count)) {
            op->count = completed;
            return DISK_RET_ECONTROLLER;
        }
        completed += count;
    }
    return DISK_RET_SUCCESS;
}

int arm64_virtio_blk_read_many_at(uint32_t index, uint64_t lba,
                                  void *out, uint32_t count)
{
    struct drive_s *drive = arm64_block_drive_at(index);
    if (!drive || !out || !count || count > 65535u)
        return -1;
    struct disk_op_s op = {
        .buf_fl = out, .drive_fl = drive, .command = CMD_READ,
        .count = (u16)count, .lba = lba,
    };
    int result = process_op(&op);
    return result == DISK_RET_SUCCESS && op.count == count ? 0 : -1;
}

int arm64_virtio_blk_read_many(uint64_t lba, void *out, uint32_t count)
{
    return arm64_virtio_blk_read_many_at(0, lba, out, count);
}

int arm64_virtio_blk_read_at(uint32_t index, uint64_t lba, void *out)
{
    return arm64_virtio_blk_read_many_at(index, lba, out, 1);
}

int arm64_virtio_blk_read(uint64_t lba, void *out)
{
    return arm64_virtio_blk_read_many(lba, out, 1);
}
