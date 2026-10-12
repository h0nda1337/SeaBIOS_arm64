# SeaBIOS-ARM64 v1.5-dev (experimental)

Milestone: [v1.0 genuine Linux ARM64 boot from MBR (no UEFI)](V1_MILESTONE.md).

The goal is a **recognizable and maintainable port of the original SeaBIOS**,
not a separately developed firmware that happens to share its name.

- Latest technical status: [PHASE13_CORE_V15.md](PHASE13_CORE_V15.md)
- Genuine Debian systemd on ext4: [DEBIAN_EXT4_ROOT.md](DEBIAN_EXT4_ROOT.md)
- Windows constraints: [WINDOWS_ARM64_CONSTRAINTS.md](WINDOWS_ARM64_CONSTRAINTS.md)
- Earlier multidrive stage: [PHASE9_MULTIDRIVE.md](PHASE9_MULTIDRIVE.md)
- Shared POST: [PHASE8_POSTFLOW.md](PHASE8_POSTFLOW.md)
- Previous shared-core stage: [PHASE5_SHARED_CORE.md](PHASE5_SHARED_CORE.md)
- Legacy MBR FAT32: [PHASE4_LEGACY_MBR.md](PHASE4_LEGACY_MBR.md)
- Previous stage: [PHASE1.md](PHASE1.md)
- Original source: [SeaBIOS upstream](https://github.com/qemu/seabios)

AArch64 build and tests:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
make ARCH=arm64 qemu-legacy-test
make ARCH=arm64 qemu-bootorder-test
make ARCH=arm64 qemu-reloc-test
make ARCH=arm64 qemu-multidrive-test
```

A genuine Debian Linux ARM64 kernel previously reached /init on QEMU in
v1.0 and v1.1, using a specially prepared MBR and initramfs. The ordinary
diagnostic MBR/GPT test images contain synthetic ARM64 machine code; the
v1.2 real-kernel regression runs separately in GitHub Actions. Do not flash
to actual ARM hardware.

The v0.8 legacy MBR/FAT32 and native PE32+ loader is not a Windows ARM64 loader, and supplies no UEFI services.
