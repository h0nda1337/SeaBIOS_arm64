#!/usr/bin/env python3
"""Round-trip checks for diagnostic payload image format."""
import sys
from pathlib import Path
import struct
import unittest

BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE / 'tools'))
from pack_payload import pack, fnv1a

class PayloadTests(unittest.TestCase):
    def test_header(self):
        code = b'abc\x00\xef'
        image = pack(code)
        self.assertEqual(image[:8], b'SBARMP01')
        self.assertEqual(struct.unpack('<II', image[8:16]), (len(code), fnv1a(code)))
        self.assertEqual(image[16:64], bytes(48))
        self.assertEqual(image[64:], code)

    def test_empty_rejected(self):
        with self.assertRaises(ValueError):
            pack(b'')

    def test_oversize_rejected(self):
        with self.assertRaises(ValueError):
            pack(b'a' * (1024 * 1024 - 63))

    def test_built_binary(self):
        binary = BASE / 'out' / 'diagnostic-payload.bin'
        if not binary.exists():
            self.skipTest('run make -f arm64/Makefile first')
        data = binary.read_bytes()
        self.assertEqual(data[:8], b'SBARMP01')
        size, checksum = struct.unpack('<II', data[8:16])
        self.assertEqual(size, len(data) - 64)
        self.assertEqual(checksum, fnv1a(data[64:]))

if __name__ == '__main__':
    unittest.main(verbosity=2)
