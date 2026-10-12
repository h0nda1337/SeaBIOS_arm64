#!/usr/bin/env python3
"""Build native FAT32 + ext4 Linux fixture from an installed Debian ARM64 tree.

Requires a genuine uncompressed Image, matching extracted Debian kernel
modules, ARM64 busybox-static, and a completed Debian minbase rootfs with
systemd-sysv. No packages are downloaded and the source rootfs is untouched.
The initramfs only loads VirtIO modules and switches to the disk's ext4 root.
Run with privileges sufficient to read every file in the rootfs.
"""
import argparse
import lzma
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from create_fat32_linux_disk import make_disk
from create_initramfs import record

SECTOR = 512

EARLY_INIT = b'''#!/bin/busybox sh
set -eu
export PATH=/bin:/sbin
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
insmod /modules/virtio_mmio.ko
insmod /modules/virtio_blk.ko
root=
for arg in $(cat /proc/cmdline); do
    case "$arg" in root=*) root=${arg#root=} ;; esac
done
test -n "$root"
for n in 1 2 3 4 5; do
    test -b "$root" && break
    sleep 1
done
mount -t ext4 -o rw "$root" /newroot
echo "SEABIOS_ARM64_SWITCH_ROOT_EXT4 $root"
mount --move /dev /newroot/dev
mount --move /proc /newroot/proc
mount --move /sys /newroot/sys
exec switch_root /newroot /sbin/init
'''

ROOT_CHECK = b'''#!/bin/sh
set -eu
echo 'SeaBIOS ARM64 Debian ext4 root verification'
. /etc/os-release
test "$ID" = debian
test "$(/usr/bin/dpkg --print-architecture)" = arm64
test "$(cat /proc/1/comm)" = systemd
grep -E '^/dev/vd[a-z][0-9]+ / ext4 rw[, ]' /proc/mounts
cat /etc/os-release
/usr/bin/dpkg --version | head -1
echo 'disk-root-write-verified' > /seabios-root-persistent-check
test "$(cat /seabios-root-persistent-check)" = disk-root-write-verified
echo SEABIOS_ARM64_DEBIAN_EXT4_SYSTEMD_OK
poweroff
'''

ROOT_SERVICE = b'''[Unit]
Description=SeaBIOS native AArch64 Debian ext4 verification
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/seabios-root-check
StandardOutput=tty
StandardError=tty
TTYPath=/dev/ttyAMA0

[Install]
WantedBy=multi-user.target
'''


def build_initramfs(busybox: Path, modules: Path) -> bytes:
    binary = busybox.read_bytes()
    if (binary[:6] != b'\x7fELF\x02\x01' or
            int.from_bytes(binary[18:20], 'little') != 183):
        raise ValueError('busybox must be a little-endian ARM64 ELF')
    contents = bytearray()
    ino = 1

    def add(name, data=b'', mode=0o040755, **kwargs):
        nonlocal ino
        contents.extend(record(name, data, mode, ino=ino, **kwargs))
        ino += 1

    for name in ('.', 'bin', 'sbin', 'dev', 'proc', 'sys', 'modules', 'newroot'):
        add(name)
    add('dev/console', mode=0o020600, major=5, minor=1)
    add('bin/busybox', binary, 0o100755)
    add('init', EARLY_INIT, 0o100755)
    # The tested Debian cloud kernel builds the VirtIO core and ext4 in;
    # only these two matching modules are needed before mounting root.
    for module in ('virtio_mmio', 'virtio_blk'):
        matches = list(modules.rglob(module + '.ko*'))
        if len(matches) != 1:
            raise ValueError(f'expected one {module} module, got {matches}')
        path = matches[0]
        data = lzma.decompress(path.read_bytes()) if path.suffix == '.xz' else path.read_bytes()
        if not data.startswith(b'\x7fELF'):
            raise ValueError('unsupported module compression: ' + str(path))
        add('modules/' + module + '.ko', data, 0o100644)
    add('TRAILER!!!', mode=0)
    return bytes(contents)


def build(image: Path, output: Path, rootfs: Path, busybox: Path,
          modules: Path, root_mib=512):
    for path in ('etc/os-release', 'usr/bin/dpkg', 'sbin/init'):
        if not (rootfs / path).exists():
            raise ValueError('completed Debian systemd rootfs missing ' + path)
    if root_mib < 256 or root_mib > 2048:
        raise ValueError('root filesystem size must be 256..2048 MiB')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='seabios-root-') as td:
        work = Path(td)
        archive = work / 'initramfs.cpio'
        archive.write_bytes(build_initramfs(busybox, modules))
        make_disk(image, output, archive)
        ext4 = work / 'root.ext4'
        with ext4.open('wb') as disk:
            disk.truncate(root_mib * 1024 * 1024)
        subprocess.run(['mke2fs', '-q', '-t', 'ext4', '-F', '-L',
                        'seabios-root', '-d', str(rootfs), str(ext4)],
                       check=True)
        script, service = work / 'root-check', work / 'root-check.service'
        script.write_bytes(ROOT_CHECK)
        service.write_bytes(ROOT_SERVICE)
        # debugfs adds only fixture files to the ext4 copy, preserving source.
        commands = work / 'debugfs.commands'
        commands.write_text(
            'mkdir /usr/local/sbin\n'
            f'write {script} /usr/local/sbin/seabios-root-check\n'
            'set_inode_field /usr/local/sbin/seabios-root-check mode 0100755\n'
            f'write {service} /etc/systemd/system/seabios-root-check.service\n'
            'mkdir /etc/systemd/system/multi-user.target.wants\n'
            'symlink /etc/systemd/system/multi-user.target.wants/seabios-root-check.service ../seabios-root-check.service\n'
        )
        subprocess.run(['debugfs', '-w', '-f', str(commands), str(ext4)],
                       check=True)
        start = (output.stat().st_size // SECTOR + 2047) & ~2047
        count = ext4.stat().st_size // SECTOR
        with output.open('r+b') as disk:
            # Partition 1 remains active FAT32; Linux owns partition 2 (ext4).
            disk.seek(462)
            disk.write(struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 0x83,
                                   b'\xfe\xff\xff', start, count))
            disk.seek(start * SECTOR)
            with ext4.open('rb') as filesystem:
                shutil.copyfileobj(filesystem, disk, 1024 * 1024)
    print(f'Debian native disk ready: ext4 LBA={start}, sectors={count}, '
          f'root=/dev/vda2, initramfs only loads modules and switch_root')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--busybox', type=Path, required=True)
    parser.add_argument('--modules', type=Path, required=True)
    parser.add_argument('--root-mib', type=int, default=512)
    args = parser.parse_args()
    build(args.image, args.output, args.rootfs, args.busybox, args.modules,
          args.root_mib)
