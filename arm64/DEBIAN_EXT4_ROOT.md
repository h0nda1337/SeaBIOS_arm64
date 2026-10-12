# v1.5: Debian ARM64 on a disk-backed ext4 root

SeaBIOS now boots an installed Debian 13 ARM64 minbase tree with systemd as
PID 1, `/usr/bin/dpkg`, and a writable ext4 root partition. The boot disk has
an active MBR FAT32 partition containing `/BOOT/IMAGE` and `/BOOT/INITRD.IMG`,
followed by an ordinary type-0x83 ext4 partition. Firmware loads the native
AArch64 Image and initramfs through SeaBIOS disk operations. Linux mounts the
root filesystem; no ext4 parser is added to firmware.

The initramfs contains Debian's ARM64 `busybox-static` and matching
`virtio_mmio.ko`/`virtio_blk.ko` modules. It mounts ext4, moves `/dev`, `/proc`
and `/sys`, and executes `switch_root /newroot /sbin/init`. The success marker
comes from a systemd service running on the disk root, after checking Debian
identity, `dpkg --print-architecture`, PID 1, the ext4 mount, and a root-file
write/read. It does not come from the old assembly `/init` test.

## Firmware configuration and placement

`-fw_cfg name=etc/linux-cmdline,file=linux-cmdline.txt` supplies a Linux command
line through the original `src/romfile.c` registry. With no file, the earlier
`console=ttyAMA0,... rdinit=/init` default remains. A supplied file must have
1..255 bytes, contain printable ASCII after removing a trailing newline/NUL,
and remain nonempty. Oversized, control-character, and embedded-NUL inputs
are rejected before loading or entering a kernel. QEMU emits a cosmetic
warning because external fw_cfg names conventionally start with `opt/`; the
SeaBIOS `etc/` namespace is used here for firmware configuration.

The loader keeps the existing `0x48000000 + text_offset` placement when both
kernel and initrd fit. Otherwise it searches the original SeaBIOS E820 RAM
map for a 2 MiB aligned base that accommodates both, respecting firmware,
DTB and FDT reservations. At 128 MiB on QEMU virt, the tested Debian kernel
loads at `0x40600000`; at 512 MiB it retains `0x48000000`. Payload ranges are
reserved only after all reads and FDT preparation succeed, preserving
fallback to another boot candidate after an error.

FDT preparation now validates the complete existing memreserve table,
rejects overflow and missing termination, and keeps the finished DTB within
the Linux AArch64 protocol's 2 MiB limit. Existing device nodes and valid
reservations are preserved. See the [Linux AArch64 boot protocol](https://docs.kernel.org/arch/arm64/booting.html).

## Reproduce the installed Debian tree

The following commands run in a Linux root shell, for example launched with
`wsl -d Debian -u root` on the development machine. They need debootstrap,
Debian archive keys, an enabled QEMU ARM64 binfmt interpreter, e2fsprogs,
Python, curl, clang/lld/llvm and qemu-system-aarch64. On Debian, QEMU user
emulation is provided by `qemu-user` and `qemu-user-binfmt`. The second stage
must actually finish; a foreign first-stage extraction alone is insufficient.

The project is `G:\SeaBIOS_arm64`, or `/mnt/g/SeaBIOS_arm64` in WSL. The
download/rootfs cache is outside the checkout under
`/var/tmp/seabios-root-linux`. Use a new cache directory for a fresh
debootstrap run; the completed tree from this session can be reused.

```sh
cache=/var/tmp/seabios-root-linux
mkdir -p "$cache"
curl -fLS --retry 3 -o "$cache/kernel.deb" \
  'https://deb.debian.org/debian-security/pool/updates/main/l/linux/linux-image-6.12.111%2Bdeb13-cloud-arm64-unsigned_6.12.111-1_arm64.deb'
printf '%s  %s\n' \
  88926feba4ab6db49c6ce072f099640cd14512af8b47a1fa2394b8d596f688da \
  "$cache/kernel.deb" | sha256sum -c -
test "$(dpkg-deb --field "$cache/kernel.deb" Architecture)" = arm64
dpkg-deb -x "$cache/kernel.deb" "$cache/kernel"

debootstrap --arch=arm64 --foreign --variant=minbase \
  --include=systemd-sysv,busybox-static,kmod \
  trixie "$cache/rootfs" https://deb.debian.org/debian
chroot "$cache/rootfs" /debootstrap/debootstrap --second-stage
cp -a "$cache/kernel/usr/lib/modules" "$cache/rootfs/usr/lib/"
chroot "$cache/rootfs" depmod -a 6.12.111+deb13-cloud-arm64

python3 - <<'PY'
import gzip
from pathlib import Path
cache = Path('/var/tmp/seabios-root-linux')
kernel = next((cache / 'kernel/boot').glob('vmlinuz-*'))
data = kernel.read_bytes()
if data[:2] == b'\x1f\x8b':
    data = gzip.decompress(data)
assert data[0x38:0x3c] == b'ARM\x64'
(cache / 'Image').write_bytes(data)
PY

cd /mnt/g/SeaBIOS_arm64
python3 arm64/tools/create_debian_root_disk.py \
  --image "$cache/Image" --output "$cache/debian-ext4.img" \
  --rootfs "$cache/rootfs" --busybox "$cache/rootfs/usr/bin/busybox" \
  --modules "$cache/kernel/usr/lib/modules"
chroot "$cache/rootfs" dpkg-query -W \
  > arm64/out/debian-root-package-manifest.txt
```

In the session's actual preparation, `kmod` was installed after the minbase
second stage using `chroot ... apt-get update` and `apt-get install -y kmod`,
then the modules were copied and `depmod` run. Including it in debootstrap
above produces the same required package set without that extra step.

The firmware fixture builder does not download anything or modify the source
rootfs. It uses `mke2fs -d` to copy that tree into a 512 MiB ext4 filesystem,
then debugfs to install the verification service in the ext4 copy. Its
default disk is 584,056,832 bytes: FAT32 partition 1 begins at LBA 2048, and
ext4 partition 2 begins at LBA 92160 with 1048576 sectors. It is not an EFI
System Partition or an EFI application. The source rootfs is approximately
300 MiB including matching kernel modules.

The recorded package inventory includes Debian systemd
`257.13-1~deb13u1`, dpkg `1.22.22`, libc6 `2.41-12+deb13u4`, busybox-static
`1:1.37.0-6+b9`, and kmod `34.2-2`. The live trixie repository can change;
the manifest and hashes identify this session, while debootstrap commands
are a reproducible procedure rather than a claim of byte-identical output.
For an identical future root package set, retain the downloaded packages or
use a Debian snapshot containing the recorded versions.

| Artifact | SHA-256 |
| --- | --- |
| Debian kernel `.deb` | `88926feba4ab6db49c6ce072f099640cd14512af8b47a1fa2394b8d596f688da` |
| Uncompressed Image, 30,892,544 bytes | `7df55571ea48d32aee64c37af99c8bbb8b57b49adb0806707218e864c3541703` |
| Debian ARM64 BusyBox ELF | `7d93682be37cf6ed46699f6fff546b80bf133cf8ec342c2d04bc23387f78bc34` |
| Session FAT32/ext4 disk | `0fe28f9d56f8c317ca136c3d4cb578bbc4a722c794fd06f9358381f7d0c40892` |

Ext4 UUIDs/timestamps and the debootstrap tree make newly built fixture disk
hashes vary. The Image header declares 31,916,032 bytes of kernel memory;
firmware zero-fills the difference from its file size. The initramfs is
2,052,496 bytes.

## Validation and evidence

```sh
make ARCH=arm64 -j2
python3 arm64/tests/test_linux_placement.py
python3 arm64/tests/test_fdt_handoff.py
python3 arm64/tests/test_qemu_root_linux.py \
  --disk /var/tmp/seabios-root-linux/debian-ext4.img \
  --log arm64/out/debian-ext4-v1.log --check-bad-cmdlines
python3 arm64/tests/test_qemu_root_linux.py \
  --disk /var/tmp/seabios-root-linux/debian-ext4.img \
  --log arm64/out/debian-ext4-v2.log --modern-virtio
```

Add `--secondary-disk` to both transports to place an unbootable disk first.
The firmware must select disk index 1; the Linux root changes to `/dev/vdb2`.
Add `--memory 128M` on disk 0 to exercise safe relocation with both
transports. Tests clone the disk to a temporary writable fixture and retain
the QEMU serial log. They stop after the root service's verification marker;
they do not claim a clean shutdown, reboot durability, networking, or a full
desktop environment.

The verified matrix is QEMU 10.0.13, cortex-a72, one CPU:

| RAM | Kernel/root disk | VirtIO-MMIO | Serial evidence |
| --- | --- | --- | --- |
| 512 MiB | disk 0, `/dev/vda2` | v1 | `out/debian-ext4-v1.log` |
| 512 MiB | disk 0, `/dev/vda2` | v2 | `out/debian-ext4-v2.log` |
| 512 MiB | disk 1, `/dev/vdb2` | v1 | `out/debian-ext4-disk1-v1.log` |
| 512 MiB | disk 1, `/dev/vdb2` | v2 | `out/debian-ext4-disk1-v2.log` |
| 128 MiB | disk 0, `/dev/vda2` | v1 | `out/debian-ext4-128m-v1.log` |
| 128 MiB | disk 0, `/dev/vda2` | v2 | `out/debian-ext4-128m-v2.log` |

`out/debian-ext4-v1.bad0.log` records rejection of 256-byte configuration;
`out/debian-ext4-v1.bad1.log` records rejection of an embedded NUL. The host
placement test exercises kernel-plus-initrd fit, fragmented/reserved memory,
overflow, default address preservation, and actual SeaBIOS romfile lookup.
The FDT handoff test covers preserved reservation entries, absent
termination, overflow, and growth beyond 2 MiB.

## Scope and next integration

This is an installed Debian minbase with native systemd startup and genuine
ext4 block I/O. It is a test deployment, with a custom early-userspace script
and root verification service. Firmware is still QEMU virt specific, has no
UEFI services, and imposes a 64 MiB Image-memory limit and 16 MiB initrd limit.
The small initramfs assumes this Debian cloud kernel builds ext4 and the
VirtIO core in; it is not a generic resolver for arbitrary distro module
dependencies. Root selection is explicit; Linux UUID/PARTUUID discovery and
distribution-generated initramfs support are next steps.

CI should run the host tests on every ARM64 change and create this Debian
root fixture in an opt-in real-Linux job with debootstrap, ARM64 binfmt and
e2fsprogs. Cache the kernel/rootfs by package manifest, validate the kernel
SHA-256 before extracting, run both VirtIO transports plus a secondary-disk
case, and upload every serial log on failure or success. Keep the small
synthetic tests and real-kernel assembly `/init` tests as separate evidence.
