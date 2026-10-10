#!/usr/bin/env python3
"""Make a *synthetic* MBR disk; its ARM64 Image header boots a UART stub."""
import argparse
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path, help='synthetic 512-byte ARM64 header image')
parser.add_argument('output', type=Path, help='raw disk image output path')
parser.add_argument('--corrupt', action='store_true', help='invalidate ARM64 Image magic')
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
mbr[510:512] = b'\x55\xaa'
args.output.parent.mkdir(exist_ok=True, parents=True)
with args.output.open('wb') as f:
    f.write(mbr)
    f.seek(start * 512)
    f.write(code)
    f.truncate((start + sectors) * 512)
