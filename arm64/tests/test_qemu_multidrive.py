#!/usr/bin/env python3
"""QEMU TCG: two distinct read-only VirtIO disks, both legacy and modern MMIO.

Checks independent SeaBIOS drive_s entries and per-device VirtIO ring reads.
Neither disk contains an operating system or a bootable filesystem.
"""
import os
from pathlib import Path
import signal
import subprocess
import tempfile

ARM = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
ROM_DIR = os.environ.get('QEMU_ROM_DIR')


def test(mode):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        # QEMU places the later -device into the first DTB slot with the
        # default layout. Verify *both* reported sizes to avoid relying on it.
        disks = [(4096, True), (6144, False)]
        args = [QEMU, *(['-L', ROM_DIR] if ROM_DIR else []), '-machine', 'virt',
                '-cpu', 'cortex-a72', '-m', '512M', '-smp', '1', '-display', 'none',
                '-serial', 'stdio', '-monitor', 'none', '-bios',
                str(ARM / 'out' / 'seabios-arm64.bin')]
        if mode == 'v2':
            args += ['-global', 'virtio-mmio.force-legacy=false']
        for index, (sectors, has_marker) in enumerate(disks):
            disk = path / f'disk{index}.img'
            with disk.open('wb') as f:
                f.truncate(sectors * 512)
            sector = bytearray(512)
            sector[510:512] = b'\x55\xaa'
            if has_marker:
                sector[256:264] = b'SBARM64!'
            with disk.open('r+b') as f:
                f.write(sector)
            args += ['-drive',f'if=none,id=d{index},format=raw,readonly=on,file={disk}',
                     '-device',f'virtio-blk-device,drive=d{index}']
        proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        try:
            data, _ = proc.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            data, _ = proc.communicate(timeout=3)
        output = data.decode(errors='replace')
        required = ['[disk] SeaBIOS disk registry: 2',
                    '[block] SeaBIOS drive_s registered (VirtIO-MMIO) index 0',
                    '[block] SeaBIOS drive_s registered (VirtIO-MMIO) index 1',
                    '[disk] secondary disk LBA0 read OK:',
                    '[disk] secondary diagnostic marker OK',
                    '[disk] LBA 0 read OK',
                    '[virtio-blk] registered disk 0, sectors: 0x0000000000001800',
                    '[virtio-blk] registered disk 1, sectors: 0x0000000000001000']
        missing = [item for item in required if item not in output]
        if missing or '[arm64] exception:' in output:
            raise AssertionError(f'{mode}: missing {missing}\n{output}')
        print(f'PASS: {mode} independent VirtIO disk registers, geometries and LBA0 reads')


if __name__ == '__main__':
    test('v1')
    test('v2')
