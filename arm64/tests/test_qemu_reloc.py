#!/usr/bin/env python3
"""QEMU test of a RELOCATED native ARM64 PE from active FAT32/MBR, no EFI."""
import tempfile
import subprocess
import sys
import os
import signal
import time
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
ROM = os.environ.get('QEMU_ROM_DIR', '')
EXECUTABLE = ARM / 'out/legacy-pe-reloc-stage.exe'
SUCCESS = '[legacy] RELOCATED ARM64 PE32+ STAGE2 OK (no EFI, not Windows)'


def attempt(mode, broken):
    with tempfile.TemporaryDirectory() as directory:
        disk = Path(directory) / 'disk.img'
        log = Path(directory) / 'serial.log'
        cmd = [sys.executable, str(ARM/'tools/create_legacy_mbr_disk.py'),
               str(EXECUTABLE), str(disk), '--pe']
        if broken:
            cmd.append('--bad-pe-reloc')
        subprocess.run(cmd, check=True)
        args = [QEMU] + (['-L', ROM] if ROM else []) + [
            '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
            '-smp', '1', '-display', 'none', '-serial', 'stdio',
            '-monitor', 'none', '-bios', str(ARM/'out/seabios-arm64.bin'),
        ] + (['-global', 'virtio-mmio.force-legacy=false'] if mode == 'v2' else []) + [
            '-drive', f'if=none,format=raw,readonly=on,file={disk},id=vd0',
            '-device', 'virtio-blk-device,drive=vd0']
        with log.open('wb') as f:
            proc = subprocess.Popen(args, stdout=f, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            try:
                expires = time.monotonic() + 7
                while time.monotonic() < expires:
                    s = log.read_text(errors='replace')
                    if SUCCESS in s or '[arm64] halt (WFE)' in s:
                        break
                    if proc.poll() is not None:
                        break
                    time.sleep(.08)
            finally:
                if proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=4)
        output = log.read_text(errors='replace')
        if broken:
            assert 'PE32+ ARM64 image rejected' in output and SUCCESS not in output, output
        else:
            assert SUCCESS in output, output
        print(f'PASS: {mode} relocated native PE (corrupt={broken})', flush=True)


if __name__ == '__main__':
    for version in ('v1', 'v2'):
        for corrupt in (False, True):
            attempt(version, corrupt)
