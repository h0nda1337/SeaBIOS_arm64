#!/usr/bin/env python3
"""Real QEMU fw_cfg regressions for late disk priorities and strict HALT.

Ten unhandled network boot entries precede a block device in QEMU's actual
bootorder. This exposes the old ARM64 3/5/10 versus fw_cfg rank collision.
All executed stages are synthetic native AArch64 payloads.
"""
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time

ARM = Path(__file__).resolve().parents[1]
QEMU = os.environ.get('QEMU', 'qemu-system-aarch64')
STAGE_OK = 'NATIVE AARCH64 MBR FAT32 STAGE2 OK (not Windows)'
DIAGNOSTIC_OK = 'CHAINLOAD OK'
HALT = '[boot] strict SeaBIOS bootorder HALT reached'


def run(active, blank, modern, case):
    args = [QEMU, *(['-L', os.environ['QEMU_ROM_DIR']]
                   if os.environ.get('QEMU_ROM_DIR') else []),
            '-machine', 'virt', '-cpu', 'cortex-a57', '-m', '256M', '-smp', '1',
            '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
            '-bios', str(ARM / 'out/seabios-arm64.bin')]
    if modern:
        args += ['-global', 'virtio-mmio.force-legacy=false']
    for index in range(1, 11):
        args += ['-device', f'virtio-net-device,bootindex={index}']
    if case == 'strict':
        args += ['-boot', 'strict=on']
    # Device insertion order is reversed by the virt MMIO slot enumeration.
    # The first disk is disk1, explicitly ranked11; the second is disk0.
    disks = [blank if case == 'strict' else active,
             blank if case == 'diagnostic' else active]
    for index, disk in enumerate(disks):
        args += ['-drive', f'if=none,id=d{index},format=raw,readonly=on,file={disk}',
                 '-device', f'virtio-blk-device,drive=d{index}' +
                 (',bootindex=11' if index == 0 else '')]
    args += ['-device', 'loader,file=' + str(ARM / 'out/diagnostic-payload.bin')
             + ',addr=0x48000000']
    process = subprocess.Popen(args, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, start_new_session=True)
    output = bytearray()
    deadline = time.monotonic() + 8
    marker = b'[arm64] halt (WFE)' if case == 'strict' else STAGE_OK.encode()
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([process.stdout], [], [], 0.15)
            if ready:
                data = os.read(process.stdout.fileno(), 65536)
                if not data:
                    break
                output.extend(data)
                if marker in output:
                    break
            if process.poll() is not None:
                break
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        tail, _ = process.communicate(timeout=3)
        output.extend(tail)
    return output.decode(errors='replace')


def main():
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        active, blank = temp / 'active.img', temp / 'blank.img'
        subprocess.run([sys.executable, str(ARM / 'tools/create_legacy_mbr_disk.py'),
                        str(ARM / 'out/legacy-stage.bin'), str(active)],
                       check=True, stdout=subprocess.DEVNULL)
        with blank.open('wb') as file:
            file.truncate(active.stat().st_size)
        for modern in (False, True):
            for case in ('late-disk', 'diagnostic', 'strict'):
                output = run(active, blank, modern, case)
                if '[arm64] exception:' in output:
                    raise AssertionError(output)
                if case == 'strict':
                    assert HALT in output, output
                    assert STAGE_OK not in output and DIAGNOSTIC_OK not in output, output
                    assert '[boot] probing:' not in output, output
                else:
                    assert '[boot] legacy drive index 1' in output, output
                    assert STAGE_OK in output and DIAGNOSTIC_OK not in output, output
                    assert '[boot] legacy drive index 0' not in output, output
                print(f'PASS: VirtIO-MMIO v{2 if modern else 1} {case}; '
                      'QEMU bootorder rank11 and original SeaBIOS BootList', flush=True)


if __name__ == '__main__':
    main()
