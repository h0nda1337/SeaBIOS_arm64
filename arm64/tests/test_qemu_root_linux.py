#!/usr/bin/env python3
"""Boot installed Debian ARM64 systemd on an ext4 disk, through native FAT32.

Supply the disk built by create_debian_root_disk.py. Success requires systemd
as PID 1, Debian dpkg ARM64, and a successful write to the ext4 root. This does
not download a distro or mistake an initramfs /init marker for disk userspace.
"""
import argparse
import os
import selectors
import shlex
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
MARKER = 'SEABIOS_ARM64_DEBIAN_EXT4_SYSTEMD_OK'


def run(disk, log_path, modern=False, secondary=False, timeout=120,
        firmware=None, bad_cmdline=None, memory='512M'):
    qemu = os.environ.get('QEMU', 'qemu-system-aarch64')
    if not shutil.which(qemu):
        raise SystemExit('qemu-system-aarch64 is missing')
    with tempfile.TemporaryDirectory(prefix='seabios-root-qemu-') as td:
        work = Path(td)
        writable = work / 'debian-root.img'
        # Guest must prove ext4 is writable; never modify the user's fixture.
        subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(disk),
                        str(writable)], check=True)
        cmdline = work / 'linux-cmdline'
        rootdev = '/dev/vdb2' if secondary else '/dev/vda2'
        args = (f'console=ttyAMA0,115200 earlycon=pl011,mmio32,0x09000000 '
                f'root={rootdev} rootfstype=ext4 rw rootwait '
                'systemd.unit=multi-user.target systemd.mask=systemd-firstboot.service')
        cmdline.write_bytes(bad_cmdline if bad_cmdline is not None else args.encode())
        cmd = [qemu, '-machine', 'virt', '-cpu', 'cortex-a72', '-m', memory,
               '-smp', '1', '-display', 'none', '-monitor', 'none', '-serial',
               'stdio', '-bios', str(firmware or ARM / 'out/seabios-arm64.bin'),
               '-fw_cfg', f'name=etc/linux-cmdline,file={cmdline}',
               '-drive', f'if=none,format=raw,file={writable},id=d0',
               '-device', 'virtio-blk-device,drive=d0']
        if secondary:
            blank = work / 'blank.img'
            with blank.open('wb') as file:
                file.truncate(2 * 1024 * 1024)
            cmd += ['-drive', f'if=none,format=raw,file={blank},id=d1',
                    '-device', 'virtio-blk-device,drive=d1']
        if modern:
            cmd += ['-global', 'virtio-mmio.force-legacy=false']
        print('Running', shlex.join(cmd), flush=True)
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        data = bytearray()
        try:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if not selector.select(1):
                    if proc.poll() is not None:
                        break
                    continue
                chunk = os.read(proc.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk)
                if (MARKER.encode() in data or
                        b'invalid etc/linux-cmdline; refusing boot' in data):
                    break
        finally:
            selector.close()
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            rest, _ = proc.communicate(timeout=5)
            data.extend(rest)
    log = data.decode(errors='replace')
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(log)
    print(log[-10000:], flush=True)
    if bad_cmdline is not None:
        if 'invalid etc/linux-cmdline; refusing boot' not in log or 'Linux version' in log:
            raise AssertionError('invalid command line was not rejected before handoff')
        print('PASS invalid Linux command-line rejection', flush=True)
        return
    required = ['Linux version', '[linux] FAT32 /BOOT/IMAGE loaded',
                '[linux] command line from etc/linux-cmdline:',
                'SEABIOS_ARM64_SWITCH_ROOT_EXT4 ' + rootdev,
                'systemd[1]:', 'Debian GNU/Linux', MARKER]
    if secondary:
        required += ['[boot] Linux drive index 1']
    for marker in required:
        if marker not in log:
            raise AssertionError('missing evidence: ' + marker + '; see ' + str(log_path))
    print('PASS installed Debian ARM64 systemd + writable ext4 root,',
          'VirtIO-MMIO v2' if modern else 'VirtIO-MMIO v1',
          'disk1' if secondary else 'disk0', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disk', type=Path, required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--modern-virtio', action='store_true')
    parser.add_argument('--secondary-disk', action='store_true')
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--memory', default='512M')
    parser.add_argument('--check-bad-cmdlines', action='store_true')
    args = parser.parse_args()
    if not args.disk.is_file():
        parser.error('Debian FAT32/ext4 fixture missing')
    run(args.disk, args.log, args.modern_virtio, args.secondary_disk,
        args.timeout, args.firmware, memory=args.memory)
    if args.check_bad_cmdlines:
        for index, bad in enumerate((b'a' * 256, b'root=/dev/vda2\x00hidden')):
            run(args.disk, args.log.with_suffix(f'.bad{index}.log'),
                args.modern_virtio, timeout=20, firmware=args.firmware,
                bad_cmdline=bad, memory=args.memory)
