#!/usr/bin/env python3
"""Verify QEMU's real fw_cfg bootorder ranks an AArch64 VirtIO MBR target.

Run twice: legacy and modern virtio-mmio. Not an OS or Windows boot test.
"""
import os
import signal
import subprocess
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
QEMU = os.getenv('QEMU', 'qemu-system-aarch64')
ROM_DIR = os.getenv('QEMU_ROM_DIR')
FIRMWARE = ARM / 'out/seabios-arm64.bin'
DISK = ARM / 'out/legacy-mbr-test-disk.img'
STAGE_SUCCESS = 'NATIVE AARCH64 MBR FAT32 STAGE2 OK (not Windows)'
PRIO_SUCCESS = '[boot] fw_cfg priority matched: active MBR FAT32 native AArch64 stage2'


def run(modern):
    flags = ['-global', 'virtio-mmio.force-legacy=false'] if modern else []
    command = ([QEMU] + (['-L', ROM_DIR] if ROM_DIR else []) + [
        '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M',
        '-smp', '1', '-nographic', '-monitor', 'none',
        '-bios', str(FIRMWARE),
        '-drive', f'if=none,format=raw,readonly=on,file={DISK},id=vd0',
        '-device', 'virtio-blk-device,drive=vd0,bootindex=1',
    ] + flags)
    p = subprocess.Popen(command, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=6)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        out, _ = p.communicate(timeout=3)
    log = out.decode(errors='replace')
    for expected in ('[fwcfg] romfile: bootorder', PRIO_SUCCESS, STAGE_SUCCESS):
        assert expected in log, f'{expected} missing from {log}'
    print(f"PASS: shared SeaBIOS fw_cfg bootorder, VirtIO-MMIO v{2 if modern else 1}")


if __name__ == '__main__':
    run(False)
    run(True)
