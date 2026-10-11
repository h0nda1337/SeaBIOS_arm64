#!/usr/bin/env python3
"""QEMU integration tests for READ-ONLY VirtIO-MMIO, MBR and Image handoff."""
import os
import signal
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
if not shutil.which(QEMU):
    sys.exit('qemu-system-aarch64 unavailable')

BASE = [QEMU, *(['-L', os.environ['QEMU_ROM_DIR']] if os.environ.get('QEMU_ROM_DIR') else []), '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
        '-smp', '1', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-bios', str(ROOT / 'out/seabios-arm64.bin')]
IMAGE = ROOT / 'out/linux-image-diagnostic.bin'
DISK = ROOT / 'out/linux-test-disk.img'

def run(args, expected, forbidden=()):
    proc = subprocess.Popen(BASE + args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    try:
        output, _ = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        output, _ = proc.communicate(timeout=3)
    text = output.decode(errors='replace')
    missing = [s for s in expected if s not in text]
    unexpected = [s for s in forbidden if s in text]
    if missing or unexpected:
        print(text)
        raise AssertionError(f'missing={missing}, forbidden={unexpected}')

for legacy in (True, False):
    mode = 'legacy' if legacy else 'modern'
    opts = [] if legacy else ['-global', 'virtio-mmio.force-legacy=false']
    with tempfile.TemporaryDirectory() as temp:
        bad = Path(temp) / 'invalid.img'
        subprocess.run([sys.executable,
                        str(ROOT / 'tools/create_linux_image_test_disk.py'),
                        str(IMAGE), str(bad), '--corrupt'], check=True)
        gpt = Path(temp) / 'gpt.img'
        bad_gpt = Path(temp) / 'bad-gpt.img'
        bad_array_gpt = Path(temp) / 'bad-array-gpt.img'
        for path, opt in [(gpt, []), (bad_gpt, ['--bad-crc']),
                          (bad_array_gpt, ['--bad-array-crc'])]:
            subprocess.run([sys.executable,
                            str(ROOT / 'tools/create_gpt_test_disk.py'),
                            str(IMAGE), str(path)] + opt, check=True)
        for case, disk, expected, forbidden in [
            ('valid', DISK,
             ['[virtio-blk] ready', '[disk] LBA 0 read OK',
              '[linux] supported ARM64 Image candidate',
              '[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)'],
             ('SYNTHETIC IMAGE TAIL CORRUPT',)),
            ('valid-gpt', gpt,
             ['[gpt] validated Linux partition candidate',
              '[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)'],
             ('SYNTHETIC IMAGE TAIL CORRUPT',)),
            ('rejected-gpt', bad_gpt,
             ['[disk] GPT primary header signature detected',
              '[boot] not found'],
             ('[linux] supported ARM64 Image candidate',)),
            ('rejected-gpt-array', bad_array_gpt,
             ['[disk] GPT primary header signature detected',
              '[boot] not found'],
             ('[linux] supported ARM64 Image candidate',)),
            ('rejected', bad,
             ['[virtio-blk] ready',
              '[linux] partition found; unsupported Image header',
              '[boot] not found'],
             ('[linux] SYNTHETIC IMAGE HANDOFF OK',)),
        ]:
            args = opts + ['-drive',
                           f'if=none,format=raw,readonly=on,file={disk},id=vd0',
                           '-device', 'virtio-blk-device,drive=vd0']
            run(args, expected, forbidden)
            print(f'PASS: {mode} VirtIO-MMIO {case} ARM64 Image')

# Phase 3 tests: optional raw initrd loaded and advertised in the new DTB.
for legacy in (True, False):
    mode = 'legacy' if legacy else 'modern'
    opts = [] if legacy else ['-global', 'virtio-mmio.force-legacy=false']
    with tempfile.TemporaryDirectory() as temp:
        initrd_disk = Path(temp) / 'initrd.img'
        bad_initrd = Path(temp) / 'bad-initrd.img'
        overlap_initrd = Path(temp) / 'overlap-initrd.img'
        for path, flag in ((initrd_disk, '--initrd'),
                           (bad_initrd, '--invalid-initrd'),
                           (overlap_initrd, '--overlap-initrd')):
            subprocess.run([sys.executable,
                            str(ROOT / 'tools/create_linux_image_test_disk.py'),
                            str(IMAGE), str(path), flag], check=True)
        run(opts + ['-drive', f'if=none,format=raw,readonly=on,file={initrd_disk},id=vd0',
                    '-device', 'virtio-blk-device,drive=vd0'],
            ['[initrd] raw partition loaded at',
             '[fdt] prepared /chosen for Linux',
             '[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)'])
        print(f'PASS: {mode} synthetic raw initrd + Linux FDT /chosen')
        run(opts + ['-drive', f'if=none,format=raw,readonly=on,file={bad_initrd},id=vd0',
                    '-device', 'virtio-blk-device,drive=vd0'],
            ['[initrd] invalid raw initrd partition', '[boot] not found'],
            ('[linux] supported ARM64 Image candidate',))
        print(f'PASS: {mode} malformed raw initrd rejected')
        run(opts + ['-drive', f'if=none,format=raw,readonly=on,file={overlap_initrd},id=vd0',
                    '-device', 'virtio-blk-device,drive=vd0'],
            ['[initrd] overlaps raw kernel partition; refusing boot', '[boot] not found'],
            ('[linux] supported ARM64 Image candidate',))
        print(f'PASS: {mode} overlapping raw initrd rejected')
