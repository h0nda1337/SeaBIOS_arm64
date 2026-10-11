#!/usr/bin/env python3
"""QEMU TCG smoke tests: native ARM64 stage2 from active MBR/FAT32.

This tests NO version of Windows, PE loader or legacy x86 boot sector.
"""
import os
import signal
import subprocess
import sys
import time
import tempfile
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
STAGE = ARM / 'out/legacy-stage.bin'
PE_STAGE = ARM / 'out/legacy-pe-stage.exe'
SUCCESS = 'NATIVE AARCH64 MBR FAT32 STAGE2 OK (not Windows)'
PROBE = '[legacy] active MBR FAT32 ARM64.BIN/ARM64.EXE candidate'
ROM_DIR = os.environ.get('QEMU_ROM_DIR')
BASE = [QEMU] + (['-L', ROM_DIR] if ROM_DIR else []) + ['-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
        '-smp', '1', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-bios', str(ARM / 'out/seabios-arm64.bin')]


def run_case(mode, flags, expected, forbidden, pe=False):
    with tempfile.TemporaryDirectory() as temp:
        img = Path(temp) / 'legacy.img'
        subprocess.run([sys.executable, str(ARM / 'tools/create_legacy_mbr_disk.py'),
                        str(PE_STAGE if pe else STAGE), str(img),
                        *(['--pe'] if pe else []), *flags], check=True)
        modern = ['-global', 'virtio-mmio.force-legacy=false'] if mode == 'v2' else []
        args = BASE + modern + [
            '-drive', f'if=none,format=raw,readonly=on,file={img},id=vd0',
            '-device', 'virtio-blk-device,drive=vd0']
        logpath = Path(temp) / 'serial.log'
        with logpath.open('wb') as logfile:
            p = subprocess.Popen(args, stdout=logfile, stderr=subprocess.STDOUT,
                                 start_new_session=True)
            try:
                until = time.monotonic() + 5
                while time.monotonic() < until:
                    log = logpath.read_text(errors='replace')
                    if SUCCESS in log or '[arm64] halt (WFE)' in log:
                        break
                    if p.poll() is not None:
                        break
                    time.sleep(0.04)
            finally:
                if p.poll() is None:
                    os.killpg(p.pid, signal.SIGKILL)
                p.wait(timeout=3)
        log = logpath.read_text(errors='replace')
        missing = [phrase for phrase in expected if phrase not in log]
        unwanted = [phrase for phrase in forbidden if phrase in log]
        if missing or unwanted:
            raise AssertionError(f'{mode} {flags}: missing={missing}, forbidden={unwanted}\n{log}')


def main():
    cases = [
        ('fragmented-stage2', [], (PROBE, SUCCESS), ('STAGE2 FAILED',)),
        ('inactive-mbr', ['--inactive'], ('[disk] MBR signature 55AA',), (PROBE, SUCCESS)),
        ('bad-mbr-signature', ['--bad-mbr'], ('[disk] LBA 0 read OK',), (PROBE, SUCCESS)),
        ('bad-bpb', ['--bad-bpb'], ('[disk] LBA 0 read OK',), (PROBE, SUCCESS)),
        ('bad-stage2-magic', ['--bad-magic'], (PROBE, 'PE32+ ARM64 image rejected'), (SUCCESS,)),
        ('bad-checksum', ['--bad-checksum'], (PROBE, 'stage2 checksum/length rejected'), (SUCCESS,)),
        ('broken-fat-chain', ['--bad-chain'], (PROBE, '[boot] invalid or not supported'), (SUCCESS,)),
        ('premature-eoc', ['--short-cluster'], (PROBE, '[boot] invalid or not supported'), (SUCCESS,)),
    ]
    completed = 0
    for mode in ('v1', 'v2'):
        for name, flags, expected, forbidden in cases:
            run_case(mode, flags, expected, forbidden)
            completed += 1
            print(f'PASS: {mode} {name}', flush=True)
    pe_cases = [
        ('native-pe32-stage2', [],
         ('[legacy] native PE32+ ARM64 stage2 -> handoff (no EFI)',
          'NATIVE ARM64 PE32+ STAGE2 OK (not Windows)'),
         ('PE32+ ARM64 image rejected',)),
        ('reject-efi-subsystem', ['--bad-pe-subsystem'],
         ('PE subsystem is not native ARM64', 'PE32+ ARM64 image rejected'),
         ('NATIVE ARM64 PE32+ STAGE2 OK',)),
        ('reject-x64-machine', ['--bad-pe-machine'],
         ('PE32+ ARM64 image rejected',), ('NATIVE ARM64 PE32+ STAGE2 OK',)),
        ('reject-wrong-image-base', ['--bad-pe-base'],
         ('PE32+ ARM64 image rejected',), ('NATIVE ARM64 PE32+ STAGE2 OK',)),
        ('reject-out-of-bounds-entry', ['--bad-pe-entry'],
         ('PE32+ ARM64 image rejected',), ('NATIVE ARM64 PE32+ STAGE2 OK',)),
        ('reject-import-table', ['--bad-pe-import'],
         ('PE32+ ARM64 image rejected',), ('NATIVE ARM64 PE32+ STAGE2 OK',)),
        ('reject-truncated-section', ['--bad-pe-section'],
         ('PE32+ ARM64 image rejected',), ('NATIVE ARM64 PE32+ STAGE2 OK',)),
    ]
    for mode in ('v1', 'v2'):
        for name, flags, expected, forbidden in pe_cases:
            run_case(mode, flags, expected, forbidden, pe=True)
            completed += 1
            print(f'PASS: {mode} {name}', flush=True)
    # Cryptographic integrity extension reusing upstream SeaBIOS src/sha256.c.
    for mode in ('v1', 'v2'):
        run_case(mode, ['--sha256'],
                 (PROBE, 'stage2 SHA-256 OK (not authenticated)', SUCCESS),
                 ('SHA-256 mismatch',))
        completed += 1
        print(f'PASS: {mode} SHA-256 legacy stage2', flush=True)
        run_case(mode, ['--sha256', '--bad-checksum'],
                 (PROBE, 'stage2 SHA-256 mismatch'),
                 (SUCCESS,))
        completed += 1
        print(f'PASS: {mode} SHA-256 corruption rejected', flush=True)
    print(f'Passed {completed}/{(len(cases) + len(pe_cases)) * 2 + 4} legacy MBR/FAT32 QEMU scenarios')

if __name__ == '__main__':
    main()
