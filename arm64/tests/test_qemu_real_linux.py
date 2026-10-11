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
    p.add_argument('--log', type=Path)
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
        subprocess.run([sys.executable,str(ROOT/'tools/create_real_linux_disk.py'),
                        str(a.image),str(disk),'--initrd',str(archive)],check=True)
        cmd=[qemu]
        if os.environ.get('QEMU_ROM_DIR'):
            cmd+=['-L',os.environ['QEMU_ROM_DIR']]
        cmd+=['-machine','virt','-cpu','cortex-a72','-m','512M','-smp','1',
              '-display','none','-monitor','none','-serial','stdio',
              '-bios',str(ROOT/'out/seabios-arm64.bin'),
              '-drive',f'if=none,format=raw,readonly=on,file={disk},id=vd0',
              '-device','virtio-blk-device,drive=vd0']
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
    print('PASS: real Linux ARM64 booted from the firmware via legacy MBR and reached /init',flush=True)


if __name__=='__main__':
    build()
