#!/usr/bin/env python3
"""Optional live QEMU smoke test; fails when QEMU is not installed."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
qemu = os.environ.get('QEMU', 'qemu-system-aarch64')
if not shutil.which(qemu):
    sys.exit('QEMU NOT INSTALLED: install qemu-system-arm and retry the runtime test')

base = [qemu, '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
        '-smp', '1', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-bios', str(ROOT / 'out' / 'seabios-arm64.bin')]

for name, suffix, expected in [
    ('no payload', [], ['SeaBIOS-ARM64 bootstrap', 'Device Tree bytes',
                        'no diagnostic payload', 'halt (WFE)']),
    ('with diagnostic payload', [
        '-device', 'loader,file=' + str(ROOT / 'out' / 'diagnostic-payload.bin')
        + ',addr=0x48000000'],
        ['SeaBIOS-ARM64 bootstrap', 'Device Tree bytes',
         'valid diagnostic payload', 'CHAINLOAD OK'])
]:
    proc = subprocess.Popen(base + suffix, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    try:
        log, _ = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        log, _ = proc.communicate()
    output = log.decode('utf-8', errors='replace')
    missing = [needle for needle in expected if needle not in output]
    if missing:
        print('QEMU OUTPUT:\n' + output)
        sys.exit(f'FAIL ({name}): missing {missing}')
    print(f'PASS: QEMU ARM64 {name}')
