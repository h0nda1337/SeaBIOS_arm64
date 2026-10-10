#!/usr/bin/env python3
"""Create synthetic GPT with one raw Image candidate; NO real Linux kernel."""
import argparse
import struct
import uuid
import zlib
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('image', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--bad-crc', action='store_true')
p.add_argument('--bad-array-crc', action='store_true')
a = p.parse_args()
image = a.image.read_bytes()
assert 64 <= len(image) <= 4096 and image[56:60] == b'ARM\x64'
image = image.ljust(4096, b'\0')
sectors, first, last = 6144, 2048, 2048 + 1024 - 1
partition_guid = uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4').bytes_le
entries = bytearray(128 * 128)
entries[:16] = partition_guid
entries[16:32] = uuid.UUID('8ad7ee64-c3e7-4c7e-a8ed-421452e43e1c').bytes_le
struct.pack_into('<QQ', entries, 32, first, last)
gpt = bytearray(512)
gpt[:8] = b'EFI PART'
struct.pack_into('<IIII', gpt, 8, 0x10000, 92, 0, 0)
struct.pack_into('<QQQQ', gpt, 24, 1, sectors - 1, 34, sectors - 34)
gpt[56:72] = uuid.UUID('cc2e5d07-6df8-47fa-8e84-5c7e59034735').bytes_le
struct.pack_into('<QIII', gpt, 72, 2, 128, 128,
                 zlib.crc32(entries) ^ (1 if a.bad_array_crc else 0))
crc = zlib.crc32(gpt[:92])
struct.pack_into('<I', gpt, 16, crc ^ (1 if a.bad_crc else 0))
mbr = bytearray(512)
mbr[446 + 4] = 0xee
struct.pack_into('<II', mbr, 446 + 8, 1, sectors - 1)
mbr[510:512] = b'\x55\xaa'
a.output.parent.mkdir(exist_ok=True, parents=True)
with a.output.open('wb') as f:
    f.write(mbr)
    f.write(gpt)
    f.write(entries)
    f.seek(first * 512)
    f.write(image)
    f.truncate(sectors * 512)
