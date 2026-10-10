#!/usr/bin/env python3
"""Test the freestanding Linux ARM64 Image header parser, not an OS boot."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ImageHeader(ctypes.Structure):
    _fields_ = [('text_offset', ctypes.c_uint64),
                ('image_size', ctypes.c_uint64),
                ('flags', ctypes.c_uint64)]

class LinuxHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name) / 'linuximage.so'
        subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', '-O2',
                        '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'linuximage.c'), '-o', str(lib)], check=True)
        cls.parse = ctypes.CDLL(str(lib)).arm64_linux_image_parse
        cls.parse.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                              ctypes.POINTER(ImageHeader)]
        cls.parse.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def parse_header(self, *, text_offset=0x80000, size=4096, flags=0,
                     sectors=4096, magic=b'ARM\x64'):
        hdr = bytearray(512)
        struct.pack_into('<QQQ', hdr, 8, text_offset, size, flags)
        hdr[56:60] = magic
        data = ctypes.create_string_buffer(bytes(hdr))
        result = ImageHeader()
        rc = self.parse(data, sectors, ctypes.byref(result))
        return rc, result

    def test_standard_linux_header(self):
        rc, data = self.parse_header()
        self.assertEqual(rc, 0)
        self.assertEqual(data.text_offset, 0x80000)
        self.assertEqual(data.image_size, 4096)

    def test_missing_magic(self):
        self.assertEqual(self.parse_header(magic=b'NOPE')[0], -2)

    def test_zero_size_legacy_header_rejected(self):
        self.assertEqual(self.parse_header(size=0)[0], -3)

    def test_partition_smaller_than_image_rejected(self):
        self.assertEqual(self.parse_header(size=4096, sectors=7)[0], -3)

    def test_big_endian_rejected(self):
        self.assertEqual(self.parse_header(flags=1)[0], -3)

    def test_text_offset_outside_two_megabytes(self):
        self.assertEqual(self.parse_header(text_offset=0x200000)[0], -3)

    def test_unaligned_text_offset_rejected(self):
        self.assertEqual(self.parse_header(text_offset=0x80001)[0], -3)

    def test_max_image_size_rejected(self):
        self.assertEqual(self.parse_header(size=64 * 1024 * 1024 + 1,
                                            sectors=200000)[0], -3)

    def test_empty_partition_rejected(self):
        self.assertEqual(self.parse_header(sectors=0)[0], -1)

    def test_null_pointers_rejected(self):
        data = ImageHeader()
        hdr = ctypes.create_string_buffer(512)
        self.assertEqual(self.parse(None, 100, ctypes.byref(data)), -1)
        self.assertEqual(self.parse(hdr, 100, None), -1)

if __name__ == '__main__':
    unittest.main(verbosity=2)
