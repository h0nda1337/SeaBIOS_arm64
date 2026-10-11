#!/usr/bin/env python3
"""Make a *synthetic* MBR disk; its ARM64 Image header boots a UART stub."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path, help='synthetic 512-byte ARM64 header image')
parser.add_argument('output', type=Path, help='raw disk image output path')
parser.add_argument('--corrupt', action='store_true', help='invalidate ARM64 Image magic')
parser.add_argument('--initrd', action='store_true', help='add a raw test initrd partition')
parser.add_argument('--invalid-initrd', action='store_true', help='invalid initrd partition bounds')
parser.add_argument('--overlap-initrd', action='store_true', help='initrd overlaps kernel partition')
args = parser.parse_args()
code = bytearray(args.image.read_bytes())
if len(code) > 16384:
    parser.error('synthetic image exceeds 32 sectors')
if len(code) < 64 or code[56:60] != b'ARM\x64':
    parser.error('not a valid synthetic ARM64 Image header')
code += bytes(16384 - len(code))
if args.corrupt:
    code[56:60] = b'NOPE'
start = 2048
sectors = 16384
mbr = bytearray(512)
part = 446
mbr[part] = 0x80
mbr[part + 4] = 0x83
struct.pack_into('<II', mbr, part + 8, start, sectors)
if args.initrd or args.invalid_initrd or args.overlap_initrd:
    initrd_start = start + (8 if args.overlap_initrd else sectors)
    initrd_sectors = 4
    p2 = 446 + 16
    mbr[p2 + 4] = 0xda
    struct.pack_into('<II', mbr, p2 + 8, initrd_start,
                     4000000 if args.invalid_initrd else initrd_sectors)
mbr[510:512] = b'\x55\xaa'
args.output.parent.mkdir(exist_ok=True, parents=True)
with args.output.open('wb') as f:
    f.write(mbr)
    f.seek(start * 512)
    f.write(code)
    if args.initrd:
        f.seek((start + sectors) * 512)
        data = bytearray(2048)
        data[:14] = b'SBINITRD_TEST!'
        data[-1] = 0x9a
        f.write(data)
    f.truncate((start + sectors + (4 if args.initrd else 0)) * 512)
