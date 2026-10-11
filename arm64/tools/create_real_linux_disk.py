#!/usr/bin/env python3
"""Construct a *legacy MBR* disk for direct AArch64 Linux Image boot.

Partition 1: type 0x83, active, raw uncompressed Linux ARM64 Image.
Partition 2: type 0xDA, raw CPIO initramfs (optional, test-only convention).

This is deliberately not a standard Linux distribution disk. There is no
UEFI, EFI System Partition, x86 VBR or EFI executable involved.
"""
import argparse
import os
import struct
from pathlib import Path

SECTOR = 512
ALIGN = 2048
LIMIT = 64 * 1024 * 1024


def align(n, boundary):
    return ((n + boundary - 1) // boundary) * boundary


def image_header(data):
    if len(data) < SECTOR or data[0x38:0x3c] != b'ARM\x64':
        raise ValueError('not an uncompressed Linux AArch64 Image (missing ARM\\x64 magic)')
    off, size, flags = struct.unpack_from('<QQQ', data, 8)
    if (off & 3) or off >= 2 * 1024 * 1024 or size < SECTOR or size > LIMIT:
        raise ValueError('unsupported ARM64 Image placement or size')
    if flags & 1:
        raise ValueError('big-endian Image unsupported')
    if len(data) > size:
        raise ValueError(f'Image file length {len(data)} exceeds size {size} in header')
    return off, size


def entry(status, kind, start, length):
    return struct.pack('<B3sB3sII', status, b'\xfe\xff\xff', kind,
                       b'\xfe\xff\xff', start, length)


def build(image: Path, output: Path, initrd: Path | None):
    data = image.read_bytes()
    off, image_size = image_header(data)
    first = ALIGN
    kernel_count = align(image_size, SECTOR) // SECTOR
    initrd_bytes = initrd.read_bytes() if initrd else b''
    if len(initrd_bytes) > 16 * 1024 * 1024:
        raise ValueError('initrd too big for current SeaBIOS-ARM64 loader')
    init_start = align(first + kernel_count, ALIGN) if initrd_bytes else 0
    init_count = align(len(initrd_bytes), SECTOR) // SECTOR
    end = align((init_start + init_count) if init_count else first + kernel_count,
                ALIGN) + ALIGN
    if end >= (1 << 32):
        raise ValueError('MBR capacity overflow')
    mbr = bytearray(SECTOR)
    mbr[446:462] = entry(0x80, 0x83, first, kernel_count)
    if init_count:
        mbr[462:478] = entry(0x00, 0xDA, init_start, init_count)
    mbr[510:512] = b'\x55\xaa'
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as file:
        file.truncate(end * SECTOR)
        file.write(mbr)
        file.seek(first * SECTOR)
        file.write(data)
        if init_count:
            file.seek(init_start * SECTOR)
            file.write(initrd_bytes)
    print(f'MBR image ready: kernel LBA={first}, bytes={len(data)}, '
          f'header-size={image_size}, offset=0x{off:x}, '
          f'initrd={len(initrd_bytes)}, sectors={end}', flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('linux_image', type=Path)
    p.add_argument('output_disk', type=Path)
    p.add_argument('--initrd', type=Path)
    a = p.parse_args()
    build(a.linux_image, a.output_disk, a.initrd)
