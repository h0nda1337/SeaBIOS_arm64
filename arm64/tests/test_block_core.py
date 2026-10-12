#!/usr/bin/env python3
"""Exercise original src/block.c with native drive registry and faulted I/O."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "include/arm64.h"
#include "block.h"
#include "std/disk.h"
#include <stdio.h>
#include <string.h>

static uint64_t capacities[] = {4096, 6144, 8192, 16384};
static int requests, fail_request;
void uart_puts(const char *s) { (void)s; }
void uart_dec(uint64_t v) { (void)v; }
void uart_putc(char c) { (void)c; }
void uart_newline(void) {}
void arm64_debug_sink(int level, const char *fmt, ...) { (void)level; (void)fmt; }
void arm64_warn_noalloc(void) {}
uint32_t arm64_virtio_blk_count(void) { return 4; }
uint64_t arm64_virtio_blk_capacity_at(uint32_t i) { return i < 4 ? capacities[i] : 0; }
int arm64_virtio_blk_read_many_transport_at(uint32_t index, uint64_t lba,
                                           void *buffer, uint32_t count)
{
    ++requests;
    if (requests == fail_request) { capacities[index] = 0; return -1; }
    if (count > 16 || count > capacities[index] - lba) return -1;
    for (uint32_t sector = 0; sector < count; ++sector)
        memset((unsigned char *)buffer + sector * 512,
               (unsigned char)(lba + sector + 17 * index), 512);
    return 0;
}

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (0)
int main(void)
{
    unsigned char buffer[64 * 1024 + 2];
    struct drive_s *drive[4];
    CHECK(!arm64_block_drive_at(0));
    block_setup();
    block_setup(); // Mapping must be idempotent and retain drive identity.
    for (unsigned i = 0; i < 4; ++i) {
        drive[i] = arm64_block_drive_at(i);
        CHECK(drive[i] == getDrive(EXTTYPE_HD, i));
        CHECK(getDriveId(EXTTYPE_HD, drive[i]) == (int)i);
        CHECK(drive[i]->sectors == capacities[i] && drive[i]->cntl_id == i);
        CHECK(drive[i]->translation == TRANSLATION_LBA);
        CHECK(drive[i]->blksize == 512);
    }
    CHECK(!getDrive(3, 0) && !getDrive(EXTTYPE_HD, 4));
    CHECK(getDriveId(EXTTYPE_HD, NULL) == -1 && getDriveId(3, drive[0]) == -1);
    CHECK(!arm64_block_drive_at(4));
    CHECK(process_op(NULL) == DISK_RET_EPARAM);
    struct disk_op_s op = {.drive_fl = drive[2], .buf_fl = buffer + 1,
                           .lba = 9, .command = CMD_READ, .count = 128};
    memset(buffer, 0xcc, sizeof(buffer));
    CHECK(process_op(&op) == 0 && op.count == 128 && requests == 8);
    CHECK(buffer[0] == 0xcc && buffer[sizeof(buffer)-1] == 0xcc);
    for (unsigned n = 0; n < 128; ++n)
        for (unsigned b = 0; b < 512; ++b)
            CHECK(buffer[1 + n*512 + b] == (unsigned char)(9 + n + 34));
    op.count = 129;
    CHECK(process_op(&op) == DISK_RET_EBOUNDARY && !op.count && requests == 8);
    op.count = 0;
    CHECK(process_op(&op) == DISK_RET_EPARAM && !op.count);
    op.count = 2; op.lba = drive[2]->sectors - 1;
    CHECK(process_op(&op) == DISK_RET_EADDRNOTFOUND && !op.count);
    op.count = 1; op.lba = UINT64_MAX;
    CHECK(process_op(&op) == DISK_RET_EADDRNOTFOUND && !op.count);
    op.lba = 0; op.count = 1; op.command = CMD_WRITE;
    CHECK(process_op(&op) == DISK_RET_EWRITEPROTECT && !op.count);
    op.count = 1; op.command = CMD_ISREADY;
    CHECK(process_op(&op) == 0 && op.count == 1);
    op.command = CMD_READ; op.count = 48;
    fail_request = requests + 2;
    memset(buffer, 0xcc, sizeof(buffer));
    CHECK(process_op(&op) == DISK_RET_ECONTROLLER && op.count == 16);
    CHECK(buffer[1 + 16*512] == 0xcc); // Failed segment never copied.
    CHECK(!arm64_block_drive_at(2));
    CHECK(getDrive(EXTTYPE_HD, 2) == drive[2]); // Dead slot retains stable ID.
    op.command = CMD_ISREADY; op.count = 1;
    CHECK(process_op(&op) == DISK_RET_ENOTREADY && !op.count);
    CHECK(arm64_virtio_blk_read_many_at(0, 4080, buffer + 1, 16) == 0);
    CHECK(arm64_virtio_blk_read_many_at(3, 2, buffer + 1, 128) == 0);
    CHECK(arm64_virtio_blk_read_many_at(2, 0, buffer + 1, 1) == -1);
    CHECK(arm64_virtio_blk_read_many_at(0, 0, buffer, 65536) == -1);
    CHECK(arm64_virtio_blk_read_many_at(0, 0, NULL, 1) == -1);
    puts("PASS: original SeaBIOS block.c registry, 64KiB reads, bounds, partial I/O and disk isolation");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'block-test.c'
        exe = Path(tmp) / 'block-test'
        src.write_text(HARNESS)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-DCONFIG_AARCH64=1',
                        '-I'+str(ROOT/'src'), '-I'+str(ROOT/'arm64'),
                        str(src), str(ROOT/'src/block.c'),
                        str(ROOT/'src/blockpolicy.c'), str(ROOT/'src/blockgeom.c'),
                        str(ROOT/'arm64/block.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
