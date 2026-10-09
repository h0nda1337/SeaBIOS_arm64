#!/usr/bin/env python3
"""Host-side regression tests for the real ARM64 DTB parser."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[1]

def be(x):
    return struct.pack('>I', x)


def pad4(buf):
    return buf + b'\0' * ((-len(buf)) % 4)


def build_dtb(*, uart=True, wrong_len=False, add_unknown=False):
    names = ('#address-cells', '#size-cells', 'device_type', 'compatible', 'reg')
    stringtab = b''
    offs = {}
    for name in names:
        offs[name] = len(stringtab)
        stringtab += name.encode() + b'\0'
    def prop(name, value):
        return be(3) + be(len(value)) + be(offs[name]) + pad4(value)
    def node(name):
        return be(1) + pad4(name.encode() + b'\0')
    s = node('')
    s += prop('#address-cells', be(2)) + prop('#size-cells', be(2))
    s += node('memory@40000000')
    s += prop('device_type', b'memory\0')
    s += prop('reg', be(0) + be(0x40000000) + be(0) + be(0x10000000))
    s += be(2)
    if uart:
        s += node('pl011@9000000')
        s += prop('compatible', b'arm,pl011\0arm,primecell\0')
        s += prop('reg', be(0) + be(0x09000000) + be(0) + be(0x1000))
        s += be(2)
    if add_unknown:
        s += node('random@a000000') + prop('compatible', b'unknown-device\0') + be(2)
    s += be(2) + be(9)
    # dtb v17 header: 10 big endian words
    reserved = bytes(16)
    off_mem_rsvmap = 40
    off_dt_struct = off_mem_rsvmap + len(reserved)
    off_dt_strings = off_dt_struct + len(s)
    total = off_dt_strings + len(stringtab)
    header = b''.join(map(be, (
        0xD00DFEED, total + (1 if wrong_len else 0), off_dt_struct,
        off_dt_strings, off_mem_rsvmap, 17, 16, 0,
        len(stringtab), len(s))))
    return header + reserved + s + stringtab


class Info(ctypes.Structure):
    _fields_ = [('ram_base', ctypes.c_uint64),
                ('ram_size', ctypes.c_uint64),
                ('uart_base', ctypes.c_uint64),
                ('dtb_bytes', ctypes.c_uint32),
                ('has_ram', ctypes.c_int),
                ('has_uart', ctypes.c_int),
                ('fwcfg_base', ctypes.c_uint64),
                ('has_fwcfg', ctypes.c_int),
                ('ram_count', ctypes.c_uint32),
                ('reserved_count', ctypes.c_uint32),
                ('ram', ctypes.c_uint64 * 16),
                ('reserved', ctypes.c_uint64 * 48),
                ('virtio_count', ctypes.c_uint32),
                ('virtio_mmio', ctypes.c_uint64 * 64)]


class FdtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.libfile = Path(cls.tmp.name) / 'libfdt_bootstrap.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
                        '-Werror', '-I' + str(BASE), str(BASE / 'fdt.c'),
                        '-o', str(cls.libfile)], check=True)
        cls.lib = ctypes.CDLL(str(cls.libfile))
        cls.lib.fdt_probe.argtypes = (ctypes.c_void_p, ctypes.POINTER(Info))
        cls.lib.fdt_probe.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def probe(self, dtb):
        buffer = ctypes.create_string_buffer(bytes(dtb))
        out = Info()
        result = self.lib.fdt_probe(buffer, ctypes.byref(out))
        return result, out

    def test_valid_virt(self):
        rc, out = self.probe(build_dtb())
        self.assertEqual(rc, 0)
        self.assertEqual(out.ram_base, 0x40000000)
        self.assertEqual(out.ram_size, 0x10000000)
        self.assertEqual(out.uart_base, 0x09000000)
        self.assertTrue(out.has_ram and out.has_uart)

    def test_no_uart(self):
        rc, out = self.probe(build_dtb(uart=False))
        self.assertEqual(rc, 0)
        self.assertTrue(out.has_ram)
        self.assertFalse(out.has_uart)

    def test_unknown_node(self):
        rc, out = self.probe(build_dtb(add_unknown=True))
        self.assertEqual(rc, 0)
        self.assertTrue(out.has_uart)

    def test_bad_magic(self):
        data = b'0000' + build_dtb()[4:]
        rc, _ = self.probe(data)
        self.assertEqual(rc, -2)

    def test_large_size(self):
        data = bytearray(build_dtb())
        data[4:8] = be(3 * 1024 * 1024)
        rc, _ = self.probe(data)
        self.assertEqual(rc, -3)

    def test_bad_string_offset(self):
        data = bytearray(build_dtb())
        data[12:16] = be(0x3fffffff)
        rc, _ = self.probe(data)
        self.assertEqual(rc, -4)

    def test_invalid_opcode(self):
        data = bytearray(build_dtb())
        data[56:60] = be(0xdeadbeef)
        rc, _ = self.probe(data)
        self.assertEqual(rc, -14)

    def test_property_length_out_of_range(self):
        data = bytearray(build_dtb())
        # First root property: begin node (8 bytes), then FDT_PROP at 64.
        data[68:72] = be(0xffffffff)
        rc, _ = self.probe(data)
        self.assertEqual(rc, -9)

if __name__ == '__main__':
    unittest.main(verbosity=2)
