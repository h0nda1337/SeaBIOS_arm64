#!/usr/bin/env python3
"""QEMU real per-disk boot selection: fallback + fw_cfg for 2 MMIO drives.

All loaded executables are synthetic native ARM64 test stages, not Windows.
The firmware must not alias the state of two separate disks.
"""
import os
import select
import time
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

ARM = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
ROM_DIR = os.environ.get('QEMU_ROM_DIR')
FIRMWARE = ARM / 'out' / 'seabios-arm64.bin'
STAGE = ARM / 'out' / 'legacy-stage.bin'
CREATE = ARM / 'tools' / 'create_legacy_mbr_disk.py'
SUCCESS = 'NATIVE AARCH64 MBR FAT32 STAGE2 OK (not Windows)'


def run(disks, modern=False, indices=None, until=SUCCESS):
    args = [QEMU, *(['-L', ROM_DIR] if ROM_DIR else []), '-machine', 'virt',
            '-cpu', 'cortex-a57', '-m', '256M', '-smp', '1', '-display', 'none',
            '-serial', 'stdio', '-monitor', 'none', '-bios', str(FIRMWARE)]
    if modern:
        args += ['-global', 'virtio-mmio.force-legacy=false']
    for n, disk in enumerate(disks):
        args += ['-drive', f'if=none,id=vd{n},format=raw,readonly=on,file={disk}',
                 '-device', 'virtio-blk-device,drive=vd' + str(n) +
                 (',bootindex=' + str(indices[n]) if indices else '')]
    p = subprocess.Popen(args, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, start_new_session=True)
    output = bytearray()
    deadline = time.monotonic() + 8
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([p.stdout], [], [], 0.15)
            if ready:
                data = os.read(p.stdout.fileno(), 65536)
                if not data:
                    break
                output.extend(data)
                if until.encode() in output:
                    break
            if p.poll() is not None:
                break
    finally:
        if p.poll() is None:
            os.killpg(p.pid, signal.SIGKILL)
        data, _ = p.communicate(timeout=3)
        output.extend(data)
    return output.decode(errors='replace')


def must(text, expected, where):
    if expected not in text or '[arm64] exception:' in text:
        raise AssertionError(f'{where}: missing {expected!r}\n{text}')


def main():
    with tempfile.TemporaryDirectory() as temp:
        base = Path(temp)
        active = base / 'stage.img'
        blank = base / 'blank.img'
        subprocess.run([sys.executable, str(CREATE), str(STAGE), str(active)],
                       check=True, stdout=subprocess.DEVNULL)
        with blank.open('wb') as f:
            f.truncate(active.stat().st_size)
        # The QEMU MMIO slot enumeration is reverse insertion order on virt.
        # The blank disk is registered as disk 0 and stage as disk 1.
        for mode in ('v1', 'v2'):
            modern = mode == 'v2'
            text = run([active, blank], modern)
            must(text, '[disk] SeaBIOS disk registry: 2', mode)
            must(text, '[boot] legacy drive index 1', mode)
            must(text, SUCCESS, mode)
            print(f'PASS: {mode} boot stage2 from disk1 (disk0 blank)', flush=True)

            # Both devices bootable: their fw_cfg priority must not collapse
            # to a single global path. A per-device rank is necessary.
            text = run([active, active], modern, indices=[1, 2])
            must(text, '[boot] shared SeaBIOS fw_cfg bootorder enabled', mode)
            must(text, '[boot] legacy drive index 1', mode)
            must(text, SUCCESS, mode)
            print(f'PASS: {mode} bootindex=1 selects disk1', flush=True)

            text = run([active, active], modern, indices=[2, 1])
            must(text, '[boot] shared SeaBIOS fw_cfg bootorder enabled', mode)
            must(text, '[boot] legacy drive index 0', mode)
            must(text, SUCCESS, mode)
            print(f'PASS: {mode} bootindex=1 selects disk0', flush=True)

        linux = base / 'linux.img'
        image = ARM / 'out/linux-image-diagnostic.bin'
        subprocess.run([sys.executable,
                        str(ARM / 'tools/create_linux_image_test_disk.py'),
                        str(image), str(linux)], check=True,
                       stdout=subprocess.DEVNULL)
        gpt = base / 'gpt.img'
        subprocess.run([sys.executable,
                        str(ARM / 'tools/create_gpt_test_disk.py'),
                        str(image), str(gpt)], check=True,
                       stdout=subprocess.DEVNULL)
        marker = 'SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)'
        for mode in ('v1', 'v2'):
            for name, disk in [('MBR', linux), ('GPT', gpt)]:
                text = run([disk, blank], mode == 'v2', until=marker)
                must(text, '[boot] Linux drive index 1', mode + name)
                must(text, marker, mode + name)
                print(f'PASS: {mode} {name} Linux Image from disk1', flush=True)



if __name__ == '__main__':
    main()
