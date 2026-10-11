#!/usr/bin/env python3
"""Unit checks for early /chosen reconstruction (not an OS boot test)."""
import ctypes
import subprocess
import struct
import tempfile
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
BE = lambda n: struct.pack('>I', n)
TOK_BEGIN, TOK_END, TOK_PROP, TOK_FINISH = 1, 2, 3, 9

def align(s):
    return s + b'\0' * (-len(s) % 4)

def make_dtb(chosen=True, nested=False, cells=2):
    names = b'#address-cells\0bootargs\0linux,initrd-start\0other\0'
    offs = {n: names.index(n + b'\0') for n in
            (b'#address-cells', b'bootargs', b'linux,initrd-start', b'other')}
    def node(name): return BE(TOK_BEGIN) + align(name + b'\0')
    def prop(name, data):
        return BE(TOK_PROP) + BE(len(data)) + BE(offs[name]) + align(data)
    s = node(b'') + prop(b'#address-cells', BE(cells))
    if chosen:
        s += node(b'chosen') + prop(b'bootargs', b'OLD\0')
        s += prop(b'linux,initrd-start', b'\0' * 8)
        if nested:
            s += node(b'child') + prop(b'other', b'preserved\0') + BE(TOK_END)
        s += BE(TOK_END)
    s += node(b'othernode') + prop(b'other', b'remains\0') + BE(TOK_END)
    s += BE(TOK_END) + BE(TOK_FINISH)
    mem = b'\0' * 16
    structoff = 40 + len(mem)
    stroff = structoff + len(s)
    header = (BE(0xd00dfeed) + BE(stroff + len(names)) + BE(structoff) +
              BE(stroff) + BE(40) + BE(17) + BE(16) + BE(0) +
              BE(len(names)) + BE(len(s)))
    return header + mem + s + names

def inspect(blob):
    def u(pos): return struct.unpack_from('>I', blob, pos)[0]
    assert u(0) == 0xd00dfeed
    assert u(4) == len(blob)
    pos, stroff, strings_size, size = u(8), u(12), u(32), u(36)
    st = blob[stroff:stroff + strings_size]
    assert st
    end = pos + size
    parents = []
    props = []
    while pos < end:
        t = u(pos)
        pos += 4
        if t == TOK_BEGIN:
            z = blob.index(b'\0', pos, end)
            parents.append(blob[pos:z].decode())
            pos = (z + 4) & ~3
        elif t == TOK_PROP:
            plen, nameoff = u(pos), u(pos + 4)
            pos += 8
            z = st.index(b'\0', nameoff)
            props.append(('/'.join(parents), st[nameoff:z].decode(), blob[pos:pos+plen]))
            pos = (pos + plen + 3) & ~3
        elif t == TOK_END:
            parents.pop()
        elif t == TOK_FINISH:
            assert not parents
            assert pos == end
            return props
        else:
            assert t == 4
    raise AssertionError('missing FDT_END')

with tempfile.TemporaryDirectory() as td:
    libpath = str(Path(td) / 'libfdt_boot.so')
    subprocess.run(['gcc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-o', libpath, str(ARM / 'fdt_boot.c')], check=True)
    lib = ctypes.CDLL(libpath)
    lib.arm64_fdt_prepare.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                     ctypes.c_uint64, ctypes.c_uint64,
                                     ctypes.POINTER(ctypes.c_void_p),
                                     ctypes.POINTER(ctypes.c_uint32)]
    lib.arm64_fdt_prepare.restype = ctypes.c_int
    count = 0
    for had_chosen in (False, True):
        for nested in (False, True):
            if nested and not had_chosen: continue
            for with_initrd in (False, True):
                src = make_dtb(had_chosen, nested)
                raw = ctypes.create_string_buffer(src)
                ptr = ctypes.c_void_p()
                length = ctypes.c_uint32()
                start, finish = (0x48200000, 0x48200800) if with_initrd else (0, 0)
                rc = lib.arm64_fdt_prepare(raw, b'console=ttyAMA0 rdinit=/init',
                                          start, finish, ctypes.byref(ptr),
                                          ctypes.byref(length))
                assert rc == 0, rc
                dst = ctypes.string_at(ptr.value, length.value)
                items = inspect(dst)
                chosen = [p for p in items if p[0] == '/chosen']
                assert [p[1] for p in chosen].count('bootargs') == 1
                assert dict((p[1],p[2]) for p in chosen)['bootargs'] == b'console=ttyAMA0 rdinit=/init\0'
                assert ('linux,initrd-start' in [p[1] for p in chosen]) == with_initrd
                assert ('linux,initrd-end' in [p[1] for p in chosen]) == with_initrd
                if with_initrd:
                    items_chosen = {p[1]:p[2] for p in chosen}
                    assert items_chosen['linux,initrd-start'] == struct.pack('>Q',start)
                    assert items_chosen['linux,initrd-end'] == struct.pack('>Q',finish)
                assert ('/othernode', 'other', b'remains\0') in items
                if nested:
                    assert ('/chosen/child','other',b'preserved\0') in items
                count += 1
    src = make_dtb()
    raw = ctypes.create_string_buffer(src)
    ptr, length = ctypes.c_void_p(), ctypes.c_uint32()
    for args, start, finish in [(b'a'*256, 0, 0), (b'x', 2, 1), (b'x', 0, 1)]:
        assert lib.arm64_fdt_prepare(raw,args,start,finish,
                ctypes.byref(ptr),ctypes.byref(length)) != 0
        count += 1
    bad = ctypes.create_string_buffer(make_dtb(cells=1))
    assert lib.arm64_fdt_prepare(bad,b'x',0,0,
            ctypes.byref(ptr),ctypes.byref(length)) != 0
    count += 1
    print(f'PASS: {count} /chosen reconstruction and invalid-input cases')
