#!/usr/bin/env python3
"""Optional live QEMU smoke test; fails when QEMU is not installed."""
import os
import signal
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
qemu = os.environ.get('QEMU', 'qemu-system-aarch64')
if not shutil.which(qemu):
    sys.exit('QEMU NOT INSTALLED: install qemu-system-arm and retry the runtime test')

base = [qemu, *(['-L', os.environ['QEMU_ROM_DIR']] if os.environ.get('QEMU_ROM_DIR') else []), '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
        '-smp', '1', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-bios', str(ROOT / 'out' / 'seabios-arm64.bin')]

for name, suffix, expected in [
    ('no payload', [], ['SeaBIOS-ARM64 legacy MBR v1.4-dev', 'Device Tree bytes',
                        '[boot] not found', 'halt (WFE)']),
    ('with diagnostic payload', [
        '-device', 'loader,file=' + str(ROOT / 'out' / 'diagnostic-payload.bin')
        + ',addr=0x48000000'],
        ['SeaBIOS-ARM64 legacy MBR v1.4-dev', 'Device Tree bytes',
         'valid diagnostic payload', 'CHAINLOAD OK'])
]:
    proc = subprocess.Popen(base + suffix, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    try:
        log, _ = proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        log, _ = proc.communicate(timeout=3)
    output = log.decode('utf-8', errors='replace')
    missing = [needle for needle in expected if needle not in output]
    if missing:
        print('QEMU OUTPUT:\n' + output)
        sys.exit(f'FAIL ({name}): missing {missing}')
    print(f'PASS: QEMU ARM64 {name}')
