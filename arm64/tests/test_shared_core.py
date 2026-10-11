#!/usr/bin/env python3
"""Host ABI tests for genuine upstream SeaBIOS shared boot-priority and SHA-256 code."""
import ctypes
import hashlib
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory() as td:
        libpath = Path(td) / 'libseabios-shared.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-DCONFIG_AARCH64=1',
                        '-I'+str(ROOT/'src'), '-o', str(libpath),
                        str(ROOT/'src/bootprio.c'), str(ROOT/'src/sha256.c')],
                       check=True)
        lib = ctypes.CDLL(str(libpath))
        lib.sha256.argtypes = (ctypes.c_char_p, ctypes.c_uint, ctypes.c_void_p)
        lib.bootprio_find_text.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        lib.bootprio_find_text.restype = ctypes.c_int
        count = 0
        for length in (0, 1, 3, 55, 56, 63, 64, 65, 1000, 4095, 65536):
            buf = (b'abc' * ((length + 2) // 3))[:length]
            result = ctypes.create_string_buffer(32)
            lib.sha256(buf, len(buf), result)
            assert result.raw == hashlib.sha256(buf).digest(), length
            count += 1

        device = b'/virtio-mmio@000000000a003e00/*'
        firmware_path = b'/virtio-mmio@000000000a003e00/disk@0,0'
        cases = [
            (device, firmware_path + b'\0', 1),
            (device, firmware_path + b'\n', 1),
            (device, b'/wrong\n' + firmware_path + b'\0', 2),
            (device, b'\n' + firmware_path + b'\r\n', 2),
            (device, firmware_path + b'   \n', 1),
            (device, b'/other\n' + firmware_path + b'\n', 2),
            (device, b'/other\n', -1),
            (b'/virtio-mmio@000000000a003e00/other', firmware_path+b'\n', -1),
            (device, firmware_path + b'\0BAD', -1),
            (device, b'A' * 4097, -1),
            (device, b'Z'*256+b'\n'+firmware_path+b'\0', 2),
            (b'HALT', b'/other\nHALT\0', 2),
        ]
        for glob, data, expected in cases:
            actual = lib.bootprio_find_text(glob, data, len(data))
            assert actual == expected, (glob, expected, actual)
            count += 1
        print(f'PASS: {count}/{count} original SeaBIOS SHA-256 / bootorder host cases')

if __name__ == '__main__':
    main()
