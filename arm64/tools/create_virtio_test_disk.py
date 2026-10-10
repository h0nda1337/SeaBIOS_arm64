#!/usr/bin/env python3
"""Create an entirely synthetic read-only disk image for VirtIO-MMIO tests."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
args = parser.parse_args()
sector0 = bytearray(512)
sector0[256:264] = b'SBARM64!'
sector0[510:512] = bytes.fromhex('55aa')
sector1 = bytearray(512)
sector1[:2] = bytes.fromhex('5aa5')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(sector0 + sector1 + bytes(512 * 126))
