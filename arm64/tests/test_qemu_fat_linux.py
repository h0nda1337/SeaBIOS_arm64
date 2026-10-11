#!/usr/bin/env python3
"""Synthetic Linux Image from active-MBR FAT32 /BOOT/IMAGE (not real Linux)."""
import os
import signal
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
QEMU = os.getenv('QEMU', 'qemu-system-aarch64')
SUCCESS = '[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)'


def run(disk, modern=False, two_disks=False):
    args = [QEMU, '-machine', 'virt', '-cpu', 'cortex-a72', '-m', '512M',
            '-smp', '1', '-display', 'none', '-serial', 'stdio',
            '-monitor', 'none', '-bios', str(ARM / 'out/seabios-arm64.bin')]
    if modern:
        args += ['-global', 'virtio-mmio.force-legacy=false']
    args += ['-drive', f'if=none,format=raw,readonly=on,file={disk},id=vd0',
             '-device', 'virtio-blk-device,drive=vd0']
    if two_disks:
        empty = disk.parent / 'blank.img'
        with empty.open('wb') as f:
            f.truncate(8 * 1024 * 1024)
        args += ['-drive', f'if=none,format=raw,readonly=on,file={empty},id=vd1',
                 '-device', 'virtio-blk-device,drive=vd1']
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            start_new_session=True)
    data = bytearray()
    try:
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            ready, _, _ = select.select([proc.stdout], [], [], .15)
            if ready:
                chunk = os.read(proc.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk)
                if SUCCESS.encode() in data or b'[boot] not found' in data:
                    break
            if proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        remaining, _ = proc.communicate(timeout=4)
        data.extend(remaining)
    return data.decode(errors='replace')


def main():
    with tempfile.TemporaryDirectory() as t:
        directory = Path(t)
        good = directory / 'good.img'
        bad = directory / 'bad.img'
        tool = ARM / 'tools/create_fat32_linux_disk.py'
        kernel = ARM / 'out/linux-image-diagnostic.bin'
        for disk, corrupt in ((good, False), (bad, True)):
            subprocess.run([sys.executable, str(tool), str(kernel), str(disk)] +
                           (['--bad-kernel-chain'] if corrupt else []), check=True)
        with_initrd = directory / 'with-initrd.img'
        fake_initrd = directory / 'initramfs.cpio'
        fake_initrd.write_bytes(b'cpio! synthetic initrd data' * 23)
        subprocess.run([sys.executable, str(tool), str(kernel), str(with_initrd),
                        '--initrd', str(fake_initrd)], check=True)
        for modern in (False, True):
            for multi in (False, True):
                text = run(good, modern=modern, two_disks=multi)
                required = ['[linux] FAT32 /BOOT/IMAGE candidate',
                            '[linux] FAT32 /BOOT/IMAGE loaded', SUCCESS]
                if multi:
                    required += ['[boot] Linux drive index 1']
                for item in required:
                    if item not in text:
                        raise AssertionError(f'missing {item}:\n{text}')
                print('PASS FAT32 Linux Image', 'v2' if modern else 'v1',
                      'disk1' if multi else 'disk0', flush=True)
            text = run(with_initrd, modern=modern)
            if '[initrd] loaded at' not in text or SUCCESS not in text:
                raise AssertionError('FAT32 initrd boot failed:\n' + text)
            print('PASS FAT32 kernel + initrd', 'v2' if modern else 'v1', flush=True)
            text = run(bad, modern=modern)
            if '[linux] FAT32 kernel chain invalid or truncated' not in text or SUCCESS in text:
                raise AssertionError('corrupt FAT chain was not rejected:\n' + text)
            print('PASS corrupt FAT rejection', 'v2' if modern else 'v1', flush=True)


if __name__ == '__main__':
    main()
