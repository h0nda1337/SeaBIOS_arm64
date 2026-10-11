# Phase 11 — native Linux Image loading from active MBR FAT32 (v1.4-dev)

**No UEFI / EFI / x86 real-mode boot sector execution.** This is an experimental
AArch64 *filesystem* boot path, not Windows ARM64 boot support.

## Contribution

- Read-only FAT32 parser extracted from the earlier ARM64 legacy stage2 loader
  into shared `arm64/fat32.c` and `arm64/include/fat32.h`.
- Both the PE32+/SBARMLE second-stage loader and Linux loader now call the
  same bounded reader and rely on the SeaBIOS-style `disk_op_s` interface.
- A bootable MBR FAT32 partition may contain `/BOOT/IMAGE` (uncompressed
  `Image`, 8.3 filename `IMAGE`) and optional `/BOOT/INITRD.IMG`.
- Fragmented files are handled using FAT cluster chains; invalid/short chains
  are rejected. FAT caches are reset on per-disk handoff.
- Both boot sources remain available: the prior *raw Linux Image partition*
  mode and the newer FAT32 *file-based* mode. Filesystem discovery uses the
  existing shared SeaBIOS `fw_cfg bootorder` priority logic.
- The kernel entry is native AArch64 with prepared DTB `x0`, optional initrd
  placement and cache maintenance. The filesystem is not exposed to Linux
  afterward by these read-only firmware services.

## Validation gates

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-fat-linux-test
make ARCH=arm64 qemu-multiboot-test
make -j2
```

The `qemu-fat-linux-test` scenarios **use a synthetic Image**, not Linux:
- VirtIO v1 and v2;
- disk 0 and disk 1, where disk 0 is unbootable;
- optional initrd;
- rejected corrupted FAT chain.

The additional GitHub Actions job must download an authentic Debian Linux
AArch64 kernel and independently confirm the standard `Linux version` banner
and `SEABIOS_ARM64_REAL_LINUX_USERSPACE_OK` after `/init` starts. Until that
job passes, do **not** claim that a real Linux kernel boots from FAT32.

## Restrictions

This is a deliberately limited 8.3-only, active-primary-MBR FAT32 reader.
FAT32 requires at least 65,525 data clusters. There is no LFN support, FAT
write support, VFAT boot menu, chainload of x86 VBR, EFI, Linux root filesystem
mount or Windows NT loader. The Linux kernel Image is capped at 64 MiB and
initrd at 16 MiB. Both firmware and QEMU integration remain experimental.

## CI tracking

The independent v1.4 GitHub Actions workflow performs synthetic FAT32,
x86/ARM64 build regressions and then attempts real Debian ARM64 Linux boot
from FAT32 on disk 1 in both VirtIO v1 and v2. A green CI result is required
to claim actual Linux-from-FAT32 userspace execution.
