#!/usr/bin/env python3
"""Host-side MBR packing tests. These do not claim an OS boot."""
import struct
import tempfile
import unittest
from pathlib import Path
from arm64.tools.create_real_linux_disk import build
from arm64.tools.create_initramfs import record

class RealLinuxDiskTests(unittest.TestCase):
    def test_disk_partition_layout_and_image_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            kernel = bytearray(4096)
            kernel[0x38:0x3c] = b'ARM\x64'
            struct.pack_into('<Q', kernel, 8, 0x80000)
            struct.pack_into('<Q', kernel, 16, 8192)
            a = root/'Image'; a.write_bytes(kernel)
            initrd = root/'init.cpio'; initrd.write_bytes(b'cpio data')
            disk = root/'disk.img'
            build(a, disk, initrd)
            with disk.open('rb') as f:
                mbr=f.read(512)
                self.assertEqual(mbr[510:512], b'\x55\xaa')
                status,_,typ,_,lba,nsec=struct.unpack_from('<B3sB3sII',mbr,446)
                self.assertEqual((status,typ,lba,nsec),(0x80,0x83,2048,16))
                f.seek(2048*512)
                self.assertEqual(f.read(4096),kernel)
                typ2=mbr[466]
                self.assertEqual(typ2,0xda)
                init_lba=struct.unpack_from('<I',mbr,470)[0]
                f.seek(init_lba*512)
                self.assertEqual(f.read(9),b'cpio data')
    def test_reject_non_arm64(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'notImage';p.write_bytes(b'not valid'*100)
            with self.assertRaises(ValueError):build(p,Path(tmp)/'disk.img',None)
    def test_cpio_header(self):
        r=record('init',b'ELF',0o100755)
        self.assertEqual(r[:6], b'070701')
        self.assertIn(b'init\0',r)

if __name__=='__main__':unittest.main()
