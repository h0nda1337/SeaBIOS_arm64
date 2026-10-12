#!/usr/bin/env python3
"""Additional Linux handoff regressions for memreserve and the 2 MiB DTB cap."""
import ctypes
import runpy
import struct
from pathlib import Path

# Reuse the existing host harness and the same byte-level DTB inspection.
fixtures = runpy.run_path(str(Path(__file__).with_name('test_fdt_boot.py')))
lib = fixtures['lib']
make_dtb = fixtures['make_dtb']
inspect = fixtures['inspect']


def prepare(blob):
    source = ctypes.create_string_buffer(bytes(blob))
    pointer, length = ctypes.c_void_p(123), ctypes.c_uint32(456)
    status = lib.arm64_fdt_prepare(source, b'root=/dev/vda2 rw', 0, 0,
                                  ctypes.byref(pointer), ctypes.byref(length))
    if status:
        assert not pointer.value and not length.value
        return status, None
    result = ctypes.string_at(pointer.value, length.value)
    inspect(result)
    assert len(result) <= 2 * 1024 * 1024
    return status, result


original = make_dtb()
base = bytearray(original)
# Insert one valid preserved reservation before the existing terminator.
reservation = struct.pack('>QQ', 0x41000000, 0x2000)
base[40:40] = reservation
for field in (4, 8, 12):
    old = struct.unpack_from('>I', base, field)[0]
    struct.pack_into('>I', base, field, old + len(reservation))
status, result = prepare(base)
assert status == 0 and result[40:72] == base[40:72]

# No reservation-map terminator must not be passed to the OS.
missing = bytearray(original)
missing[40:56] = reservation
assert prepare(missing)[0] == -22

# Overflow in a preserved range must be rejected even if /chosen is valid.
overflow = bytearray(base)
overflow[40:56] = struct.pack('>QQ', (1 << 64) - 4096, 8192)
assert prepare(overflow)[0] == -22

# Input at the boot protocol limit is valid FDT, but adding /chosen bytes
# would exceed Linux's limit. Unused strings pad without changing structure.
large = bytearray(original)
padding = 2 * 1024 * 1024 - len(large)
large.extend(b'\0' * padding)
struct.pack_into('>I', large, 4, len(large))
strings = struct.unpack_from('>I', large, 32)[0]
struct.pack_into('>I', large, 32, strings + padding)
assert prepare(large)[0] != 0
print('PASS: preserved memreserve, missing terminator, overflow and Linux DTB cap')
