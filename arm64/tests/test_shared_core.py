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
        # These comparisons mirror upstream SeaBIOS's x86 IPL BootList:
        #  priority, device class and (for disks) controller identity.
        class BootKey(ctypes.Structure):
            _fields_ = [('priority', ctypes.c_int), ('type', ctypes.c_int),
                        ('drive_type', ctypes.c_uint), ('controller_id', ctypes.c_uint)]
        lib.bootprio_precedes.argtypes = (ctypes.POINTER(BootKey), ctypes.POINTER(BootKey))
        lib.bootprio_precedes.restype = ctypes.c_int
        testcases = [
            ((1, 2, 0, 0), (2, 1, 0, 0), True),
            ((3, 2, 0, 0), (2, 3, 0, 0), False),
            ((2, 1, 0, 0), (2, 2, 0, 0), True),
            ((2, 3, 0, 0), (2, 2, 0, 0), False),
            ((2, 2, 1, 0), (2, 2, 2, 0), True),
            ((2, 2, 2, 0), (2, 2, 1, 0), False),
            ((2, 2, 2, 5), (2, 2, 2, 6), True),
            ((2, 2, 2, 6), (2, 2, 2, 5), False),
            ((2, 2, 2, 5), (2, 2, 2, 5), False),
            ((2, 128, 0, 0), (2, 128, 0, 0), False),
            ((2, 128, 3, 4), (2, 128, 5, 1), False),
            ((-1, 2, 0, 0), (0, 2, 0, 0), True),
        ]
        for before, after, expected in testcases:
            result = bool(lib.bootprio_precedes(ctypes.byref(BootKey(*before)),
                                                ctypes.byref(BootKey(*after))))
            assert result == expected, (before, after, expected, result)
            count += 1
        print(f'PASS: {count}/{count} original SeaBIOS shared-core host cases')

if __name__ == '__main__':
    main()
