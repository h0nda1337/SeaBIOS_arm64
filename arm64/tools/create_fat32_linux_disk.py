#!/usr/bin/env python3
"""Create read-only active-MBR FAT32 fixture: /BOOT/IMAGE + optional INITRD.IMG.

This is a native ARM64 Linux Image fixture, not an EFI System Partition.
Uses 8.3 entries, deliberately fragmented IMAGE clusters and a sparse disk.
"""
import argparse
import struct
from pathlib import Path

SECTOR = 512
START = 2048
RESERVED = 32
FAT_SECTORS = 2048
SPC = 1
ROOT = 2
BOOT = 3


def pad_sectors(data):
    return (len(data) + SECTOR - 1) // SECTOR


def dirent(name, attrs, first, length):
    entry = bytearray(32)
    entry[:11] = name
    entry[11] = attrs
    struct.pack_into('<H', entry, 20, first >> 16)
    struct.pack_into('<H', entry, 26, first & 0xffff)
    struct.pack_into('<I', entry, 28, length)
    return entry


def make_disk(image, output, initrd=None, bad_kernel_chain=False):
    image_data = Path(image).read_bytes()
    initrd_data = Path(initrd).read_bytes() if initrd else b''
    if len(image_data) < SECTOR or image_data[0x38:0x3c] != b'ARM\x64':
        raise ValueError('expected raw, uncompressed AArch64 Linux Image')
    if len(image_data) > 64 * 1024 * 1024 or len(initrd_data) > 16 * 1024 * 1024:
        raise ValueError('firmware image/initrd size limit exceeded')
    kcount = pad_sectors(image_data)
    icount = pad_sectors(initrd_data)
    # Non-contiguous first clusters detect a reader that wrongly treats FAT32
    # files as raw extents. All later clusters are contiguous for test speed.
    kchain = ([4, 6, 8] + list(range(10, 10 + max(0, kcount - 3))))[:kcount]
    ichain = list(range(max(kchain, default=9) + 2,
                        max(kchain, default=9) + 2 + icount))
    max_cluster = max([ROOT, BOOT] + kchain + ichain)
    total = max(90000, RESERVED + FAT_SECTORS + max_cluster + 4096)
    if max_cluster + 2 > FAT_SECTORS * SECTOR // 4:
        raise ValueError('test disk exceeds FAT entry allocation')
    mbr = bytearray(SECTOR)
    mbr[446] = 0x80
    mbr[450] = 0x0c
    struct.pack_into('<II', mbr, 454, START, total)
    mbr[-2:] = b'\x55\xaa'
    bpb = bytearray(SECTOR)
    bpb[:3] = b'\xeb\x58\x90'
    bpb[3:11] = b'SBARM64 '
    struct.pack_into('<H', bpb, 11, SECTOR)
    bpb[13] = SPC
    struct.pack_into('<H', bpb, 14, RESERVED)
    bpb[16] = 1
    bpb[21] = 0xf8
    struct.pack_into('<I', bpb, 32, total)
    struct.pack_into('<I', bpb, 36, FAT_SECTORS)
    struct.pack_into('<I', bpb, 44, ROOT)
    bpb[82:90] = b'FAT32   '
    bpb[-2:] = b'\x55\xaa'
    fat = bytearray(FAT_SECTORS * SECTOR)
    for index, value in ((0, 0x0ffffff8), (1, 0x0fffffff),
                         (ROOT, 0x0fffffff), (BOOT, 0x0fffffff)):
        struct.pack_into('<I', fat, 4 * index, value)
    for chain in (kchain, ichain):
        for i, cluster in enumerate(chain):
            val = chain[i + 1] if i + 1 < len(chain) else 0x0fffffff
            if bad_kernel_chain and chain is kchain and i == 0:
                val = 0x0fffffff
            struct.pack_into('<I', fat, 4 * cluster, val)
    root = bytearray(SECTOR)
    root[:32] = dirent(b'BOOT       ', 0x10, BOOT, 0)
    boot = bytearray(SECTOR)
    boot[:32] = dirent(b'IMAGE      ', 0x20, kchain[0], len(image_data))
    if ichain:
        boot[32:64] = dirent(b'INITRD  IMG', 0x20, ichain[0], len(initrd_data))
    data_start = START + RESERVED + FAT_SECTORS
    with Path(output).open('wb') as f:
        f.truncate((START + total) * SECTOR)
        def wr(lba, blob):
            f.seek(lba * SECTOR)
            f.write(blob)
        wr(0, mbr)
        wr(START, bpb)
        wr(START + RESERVED, fat)
        wr(data_start + ROOT - 2, root)
        wr(data_start + BOOT - 2, boot)
        for contents, chain in ((image_data, kchain), (initrd_data, ichain)):
            for i, cluster in enumerate(chain):
                wr(data_start + cluster - 2,
                   contents[i * SECTOR:(i + 1) * SECTOR].ljust(SECTOR, b'\0'))
    print(f'FAT32 MBR disk: kernel={len(image_data)} initrd={len(initrd_data)} '
          f'clusters={max_cluster + 1}, sectors={START + total}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--initrd', type=Path)
    parser.add_argument('--bad-kernel-chain', action='store_true')
    a = parser.parse_args()
    make_disk(a.image, a.output, a.initrd, a.bad_kernel_chain)
