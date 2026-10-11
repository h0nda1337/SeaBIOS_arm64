#!/usr/bin/env python3
"""Host-side ABI tests of architecture-shared SeaBIOS block operation policy."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "block.h"
#include "blockpolicy.h"
#include "std/disk.h"
#include <stdio.h>

static int test(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); return 1; }
    return 0;
}
int main(void)
{
    struct drive_s drive = { .type = DTYPE_VIRTIO_BLK, .blksize = 512,
                             .sectors = 8192 };
    struct disk_op_s op = { .drive_fl = &drive, .command = CMD_READ,
                            .count = 128, .lba = 0 };
    int failures = 0;
    failures += test(blockop_prepare(&op, drive.blksize) == DISK_RET_SUCCESS
                     && op.count == 128, "exact 64KiB accepted");
    op.count = 129;
    failures += test(blockop_prepare(&op, drive.blksize) == DISK_RET_EBOUNDARY
                     && op.count == 0, "over 64KiB rejected with zero count");
    op.count = 4;
    failures += test(blockop_finish(&op, 4, DISK_RET_ECONTROLLER) == DISK_RET_ECONTROLLER
                     && op.count == 0, "failed operation count cleared");
    op.count = 2;
    failures += test(blockop_finish(&op, 4, DISK_RET_ECONTROLLER) == DISK_RET_ECONTROLLER
                     && op.count == 2, "partial-transfer count preserved");
    op.count = 4;
    failures += test(blockop_finish(&op, 4, DISK_RET_SUCCESS) == DISK_RET_SUCCESS
                     && op.count == 4, "successful operation count preserved");
    op.count = 17;
    failures += test(blockop_prepare(&op, 4096) == DISK_RET_EBOUNDARY
                     && op.count == 0, "large sector bytes validated");
    for (int cmd = 0; cmd < 256; cmd++) {
        op.command = (u8)cmd;
        int expected = (cmd == CMD_FORMAT || cmd == CMD_RESET ||
                        cmd == CMD_ISREADY || cmd == CMD_VERIFY ||
                        cmd == CMD_SEEK) ? DISK_RET_SUCCESS : DISK_RET_EPARAM;
        failures += test(default_process_op(&op) == expected,
                         "upstream SeaBIOS fallback command policy");
    }
    if (!failures) puts("PASS: 262/262 shared SeaBIOS block policy host cases");
    return !!failures;
}
'''

def main():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        src = path / 'test.c'
        exe = path / 'test'
        src.write_text(TEST)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-std=gnu11',
                        '-DCONFIG_AARCH64=1', '-I'+str(ROOT/'src'),
                        '-o', str(exe), str(src), str(ROOT/'src/blockpolicy.c')],
                       check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
