#!/usr/bin/env python3
"""Build tiny CPIO newc initramfs for the *actual* Linux AArch64 init ELF."""
import argparse
from pathlib import Path


def pad4(b):
    return b + b'\0' * ((-len(b)) % 4)


def record(name: str, data: bytes, mode: int, major=0, minor=0, ino=1):
    nb = name.encode() + b'\0'
    numbers = [ino, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(nb), 0]
    h = b'070701' + b''.join(f'{num:08x}'.encode() for num in numbers)
    assert len(h) == 110
    return pad4(h + nb) + pad4(data)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('init_elf', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    elf = args.init_elf.read_bytes()
    if elf[:4] != b'\x7fELF' or elf[4] != 2 or elf[5] != 1 or int.from_bytes(elf[18:20], 'little') != 183:
        raise ValueError('Expected 64-bit little-endian ARM64 ELF')
    out = (record('.', b'', 0o040755, ino=1)
           + record('dev', b'', 0o040755, ino=2)
           + record('dev/console', b'', 0o020600, major=5, minor=1, ino=3)
           + record('init', elf, 0o100755, ino=4)
           + record('TRAILER!!!', b'', 0, ino=5))
    args.output.write_bytes(out)
    print(f'initramfs created: {len(out)} bytes')


if __name__ == '__main__':
    main()
