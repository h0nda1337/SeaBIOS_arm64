#!/usr/bin/env python3
"""Create a sparse FAT32 active-MBR image with a native AArch64 stage2.

Diagnostic format is custom (SBARMLE1), not Windows and not an x86 VBR.
Only used for reproducible QEMU firmware smoke tests.
"""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('stage', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--inactive', action='store_true')
p.add_argument('--pe', action='store_true', help='raw synthetic PE32+ file')
p.add_argument('--bad-pe-subsystem', action='store_true')
p.add_argument('--bad-pe-machine', action='store_true')
p.add_argument('--bad-pe-base', action='store_true')
p.add_argument('--bad-pe-entry', action='store_true')
p.add_argument('--bad-pe-import', action='store_true')
p.add_argument('--bad-pe-section', action='store_true')
p.add_argument('--bad-mbr', action='store_true')
p.add_argument('--bad-bpb', action='store_true')
p.add_argument('--bad-checksum', action='store_true')
p.add_argument('--bad-chain', action='store_true')
p.add_argument('--bad-magic', action='store_true')
p.add_argument('--short-cluster', action='store_true')
a = p.parse_args()

stage = a.stage.read_bytes()
assert len(stage) == 1024 if not a.pe else len(stage) >= 512
fnv = 2166136261
for b in stage:
    fnv = ((fnv ^ b) * 16777619) & 0xffffffff
if a.bad_checksum: fnv ^= 0x1
header = bytearray(64)
header[:8] = b'SBARMLE1' if not a.bad_magic else b'MZARMLE1'
struct.pack_into('<II', header, 8, len(stage), fnv)
filedata = bytearray(stage if a.pe else header + stage)
if a.pe:
    peoffset = struct.unpack_from('<I', filedata, 0x3c)[0]
    opt = peoffset + 24
    if a.bad_pe_subsystem:
        struct.pack_into('<H', filedata, opt + 68, 10)
    if a.bad_pe_machine:
        struct.pack_into('<H', filedata, peoffset + 4, 0x8664)
    if a.bad_pe_base:
        struct.pack_into('<Q', filedata, opt + 24, 0x50000000)
    if a.bad_pe_entry:
        struct.pack_into('<I', filedata, opt + 16, 0x100000)
    if a.bad_pe_import:
        struct.pack_into('<II', filedata, opt + 112 + 8, 0x1000, 16)
    if a.bad_pe_section:
        first_section = opt + struct.unpack_from('<H', filedata, peoffset + 20)[0]
        struct.pack_into('<I', filedata, first_section + 20, len(filedata) + 0x200)
assert len(filedata) <= 1536

START = 2048
TOTAL = 80000
FATSECTORS = 650
RESERVED = 32
DATA = RESERVED + FATSECTORS
ROOT = 2
BOOT = 3
FILE_CHAIN = [4, 6, 9]


def wr(f, lba, data):
    assert len(data) == 512
    f.seek(lba * 512)
    f.write(data)


def dirent(name, attr, clus, length):
    e = bytearray(32)
    e[:11] = name
    e[11] = attr
    struct.pack_into('<H', e, 20, clus >> 16)
    struct.pack_into('<H', e, 26, clus & 0xffff)
    struct.pack_into('<I', e, 28, length)
    return e

mbr = bytearray(512)
mbr[446] = 0 if a.inactive else 0x80
mbr[446 + 4] = 0x0c
struct.pack_into('<II', mbr, 446 + 8, START, TOTAL)
mbr[510:512] = b'\x55\xaa' if not a.bad_mbr else b'\0\0'

bpb = bytearray(512)
bpb[0:3] = b'\xeb\x58\x90'
bpb[3:11] = b'SBARM64 '
struct.pack_into('<H', bpb, 11, 512 if not a.bad_bpb else 4096)
bpb[13] = 1
struct.pack_into('<H', bpb, 14, RESERVED)
bpb[16] = 1
bpb[21] = 0xf8
struct.pack_into('<I', bpb, 32, TOTAL)
struct.pack_into('<I', bpb, 36, FATSECTORS)
struct.pack_into('<I', bpb, 44, ROOT)
bpb[64] = 0x80
bpb[82:90] = b'FAT32   '
bpb[510:512] = b'\x55\xaa'

fat = bytearray(512)
for cluster, value in [(0, 0x0ffffff8), (1, 0x0fffffff), (ROOT, 0x0fffffff),
                       (BOOT, 0x0fffffff), (4, 0x0fffffff if a.short_cluster else 6),
                       (6, 0x0fffffff if a.bad_chain else 9), (9, 0x0fffffff)]:
    struct.pack_into('<I', fat, cluster * 4, value)
root = bytearray(512)
root[0:32] = dirent(b'BOOT       ', 0x10, BOOT, 0)
boot = bytearray(512)
boot[0:32] = dirent(b'ARM64   BIN', 0x20, FILE_CHAIN[0], len(filedata))

with a.output.open('wb') as f:
    f.truncate((START + TOTAL) * 512)
    wr(f, 0, mbr)
    wr(f, START, bpb)
    wr(f, START + RESERVED, fat)
    wr(f, START + DATA + ROOT - 2, root)
    wr(f, START + DATA + BOOT - 2, boot)
    for idx, cluster in enumerate(FILE_CHAIN):
        block = filedata[idx * 512:(idx+1)*512].ljust(512, b'\0')
        wr(f, START + DATA + cluster - 2, block)
