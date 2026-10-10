#!/usr/bin/env python3
"""Host regression tests for GPT CRC32 primitive, including streaming."""
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]

class GptCrcTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name) / 'gptcrc.so'
        subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', '-O2',
                        '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'gptcrc.c'), '-o', str(lib)], check=True)
        cls.crc = ctypes.CDLL(str(lib)).arm64_crc32
        cls.crc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
        cls.crc.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check(self, blob):
        raw = ctypes.create_string_buffer(blob)
        self.assertEqual(self.crc(raw, len(blob), 0xffffffff) ^ 0xffffffff,
                         zlib.crc32(blob))

    def test_classic_vector(self):
        self.check(b'123456789')

    def test_empty(self):
        self.check(b'')

    def test_one_gpt_header(self):
        self.check(b'EFI PART' + b'\0' * 84)

    def test_streaming_matches_single_pass(self):
        data = bytes(range(256)) * 64
        expect = zlib.crc32(data)
        state = 0xffffffff
        for start in range(0, len(data), 512):
            chunk = ctypes.create_string_buffer(data[start:start+512])
            state = self.crc(chunk, len(chunk) - 1, state)
        self.assertEqual(state ^ 0xffffffff, expect)

    def test_deterministic_random_vectors(self):
        rng = random.Random(42001)
        for size in (1, 3, 128, 511, 512, 1024, 4096):
            self.check(rng.randbytes(size))

if __name__ == '__main__':
    unittest.main(verbosity=2)
