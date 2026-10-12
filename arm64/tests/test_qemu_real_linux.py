#!/usr/bin/env python3
"""Explicit real Linux AArch64 QEMU integration test, without UEFI.

The kernel must be a genuine, uncompressed ARM64 Linux Image, supplied via
--image or ARM64_REAL_LINUX_IMAGE. This never treats the synthetic test Image
as a real Linux kernel.
"""
import argparse
import os
import shlex
import shutil
import signal
import selectors
import time
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parent


def build():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image', type=Path, default=Path(os.environ.get('ARM64_REAL_LINUX_IMAGE','/nonexistent/linux/Image')))
    p.add_argument('--timeout', type=int, default=95)
    p.add_argument('--modern-virtio', action='store_true')
    p.add_argument('--fat32', action='store_true', help='load /BOOT/IMAGE from active-MBR FAT32')
    p.add_argument('--secondary-disk', action='store_true',
                   help='boot Linux from disk1 while disk0 is unbootable')
    p.add_argument('--log', type=Path)
    p.add_argument('--bios', type=Path, default=ROOT/'out/seabios-arm64.bin',
                   help='firmware binary, including a saved baseline for comparison')
    a=p.parse_args()
    if not a.image.is_file():
        p.error('real ARM64 kernel Image missing (set ARM64_REAL_LINUX_IMAGE)')
    qemu=os.environ.get('QEMU','qemu-system-aarch64')
    if not shutil.which(qemu):
        p.error('qemu-system-aarch64 executable missing')
    with tempfile.TemporaryDirectory() as td:
        td=Path(td)
        init=td/'init'
        archive=td/'initramfs.cpio'
        disk=td/'linux-mbr.img'
        subprocess.run([os.environ.get('CLANG','clang'),'--target=aarch64-linux-gnu',
                        '-nostdlib','-static','-fuse-ld=lld','-Wl,-e,_start',
                        '-o',str(init),str(ROOT/'tests/linux_init.S')],check=True)
        subprocess.run([sys.executable,str(ROOT/'tools/create_initramfs.py'),
                        str(init),str(archive)],check=True)
        builder = ('create_fat32_linux_disk.py' if a.fat32 else
                   'create_real_linux_disk.py')
        subprocess.run([sys.executable,str(ROOT/'tools'/builder),
                        str(a.image),str(disk),'--initrd',str(archive)],check=True)
        cmd=[qemu]
        if os.environ.get('QEMU_ROM_DIR'):
            cmd+=['-L',os.environ['QEMU_ROM_DIR']]
        cmd+=['-machine','virt','-cpu','cortex-a72','-m','512M','-smp','1',
              '-display','none','-monitor','none','-serial','stdio',
              '-bios',str(a.bios),
              '-drive',f'if=none,format=raw,readonly=on,file={disk},id=vd0',
              '-device','virtio-blk-device,drive=vd0']
        if a.secondary_disk:
            # QEMU virt enumerates the most recently attached MMIO device
            # first. Add the non-bootable drive second to force Linux onto disk1.
            blank = td / 'blank.img'
            with blank.open('wb') as f:
                f.truncate(2 * 1024 * 1024)
            cmd += ['-drive', f'if=none,format=raw,readonly=on,file={blank},id=vd1',
                    '-device', 'virtio-blk-device,drive=vd1']
        if a.modern_virtio:
            cmd+=['-global','virtio-mmio.force-legacy=false']
        print('Running',shlex.join(cmd),flush=True)
        proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              start_new_session=True)
        data=bytearray()
        selector=selectors.DefaultSelector()
        selector.register(proc.stdout,selectors.EVENT_READ)
        deadline=time.monotonic()+a.timeout
        marker=b'SEABIOS_ARM64_REAL_LINUX_USERSPACE_OK'
        try:
            while time.monotonic()<deadline:
                events=selector.select(timeout=1)
                if not events:
                    if proc.poll() is not None:
                        break
                    continue
                chunk=os.read(proc.stdout.fileno(),65536)
                if not chunk:
                    break
                data.extend(chunk)
                if marker in data:
                    break
        finally:
            selector.close()
            if proc.poll() is None:
                os.killpg(proc.pid,signal.SIGKILL)
            proc.wait(timeout=5)
        output=bytes(data)
    log=output.decode('utf-8',errors='replace')
    if a.log:
        a.log.parent.mkdir(parents=True,exist_ok=True)
        a.log.write_text(log)
    # Print a bounded tail; the full log is available on CI as an artifact.
    print(log[-18000:],flush=True)
    marker='SEABIOS_ARM64_REAL_LINUX_USERSPACE_OK'
    if marker not in log:
        raise SystemExit('FAIL: real Linux AArch64 did NOT reach /init')
    if 'Linux version' not in log:
        raise SystemExit('FAIL: no evidence of Linux kernel initialization')
    if a.fat32 and '[linux] FAT32 /BOOT/IMAGE loaded' not in log:
        raise SystemExit('FAIL: FAT32 file loader not used')
    if a.secondary_disk and '[boot] Linux drive index 1' not in log:
        raise SystemExit('FAIL: Linux was not loaded from the second disk')
    print('PASS: real Linux ARM64 booted from the firmware via legacy MBR and reached /init',flush=True)


if __name__=='__main__':
    build()
