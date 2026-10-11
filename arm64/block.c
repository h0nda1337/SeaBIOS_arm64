/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* SeaBIOS drive_s/disk_op_s command interface for AArch64 VirtIO-MMIO.
 * x86 continues to dispatch in src/block.c, using shared blockpolicy.c.
 * The physical hardware backend still lives in src/hw/virtio-mmio.c. */
#include "include/arm64.h"
#include "../src/block.h"
#include "../src/blockpolicy.h"
#include "../src/std/disk.h"

/* The transport is private; all existing ARM64 boot consumers call the
 * public wrappers below, enforcing the SeaBIOS disk-operation contract. */
extern int arm64_virtio_blk_read_many_transport(uint64_t lba, void *buf,
                                                uint32_t count);

static struct drive_s VirtioDrive;
static int DriveRegistered;

static struct drive_s *
arm64_block_drive(void)
{
    uint64_t sectors = arm64_virtio_blk_capacity();
    if (!sectors)
        return NULL;
    if (!DriveRegistered) {
        VirtioDrive.type = DTYPE_VIRTIO_BLK;
        VirtioDrive.blksize = DISK_SECTOR_SIZE;
        VirtioDrive.sectors = sectors;
        /* Only one synchronous VirtIO-MMIO block device supported for now. */
        VirtioDrive.cntl_id = 0;
        DriveRegistered = 1;
        uart_puts("[block] SeaBIOS drive_s registered (VirtIO-MMIO)\n");
    }
    VirtioDrive.sectors = sectors;
    return &VirtioDrive;
}

/* AArch64 process_op intentionally uses the original struct disk_op_s and
 * returns standard SeaBIOS BIOS disk statuses. There is no INT 13h ABI. */
int
process_op(struct disk_op_s *op)
{
    if (!op || !op->drive_fl ||
        (op->command == CMD_READ && !op->buf_fl))
        return DISK_RET_EPARAM;
    u16 origcount = op->count;
    int status = blockop_prepare(op, op->drive_fl->blksize);
    if (status)
        return status;
    if (op->drive_fl != arm64_block_drive() ||
        op->drive_fl->type != DTYPE_VIRTIO_BLK)
        status = DISK_RET_EPARAM;
    else if (op->command != CMD_READ)
        status = default_process_op(op);
    else if (!op->count)
        status = DISK_RET_EPARAM;
    else if (op->lba >= op->drive_fl->sectors ||
             op->count > op->drive_fl->sectors - op->lba)
        status = DISK_RET_EADDRNOTFOUND;
    else if (op->count > 16)
        status = DISK_RET_EBOUNDARY; /* backend ring currently max 16 */
    else
        status = arm64_virtio_blk_read_many_transport(
            op->lba, op->buf_fl, op->count)
            ? DISK_RET_ECONTROLLER : DISK_RET_SUCCESS;
    return blockop_finish(op, origcount, status);
}

int arm64_virtio_blk_read_many(uint64_t lba, void *out, uint32_t count)
{
    struct drive_s *drive = arm64_block_drive();
    if (!drive || !out || !count || count > 65535u)
        return -1;
    struct disk_op_s op = {
        .buf_fl = out, .drive_fl = drive, .command = CMD_READ,
        .count = (u16)count, .lba = lba,
    };
    int result = process_op(&op);
    return result == DISK_RET_SUCCESS && op.count == count ? 0 : -1;
}

int arm64_virtio_blk_read(uint64_t lba, void *out)
{
    return arm64_virtio_blk_read_many(lba, out, 1);
}
