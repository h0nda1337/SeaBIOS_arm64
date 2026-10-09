#!/usr/bin/env python3
"""Create the optional SeaBIOS-ARM64 diagnostic handoff image."""
import argparse
import pathlib
import struct

HEADER = 64
MAX_CODE = 1024 * 1024 - HEADER


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def pack(code):
    if not 0 < len(code) <= MAX_CODE:
        raise ValueError("diagnostic code length outside safe slot")
    return b"SBARMP01" + struct.pack("<II", len(code), fnv1a(code)) + bytes(48) + code


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input", type=pathlib.Path)
    p.add_argument("output", type=pathlib.Path)
    args = p.parse_args()
    args.output.write_bytes(pack(args.input.read_bytes()))
    print(f"[arm64] packed {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
