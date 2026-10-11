# SeaBIOS-ARM64 v1.0-experimental

Milestone: [v1.0 genuine Linux ARM64 boot from MBR (no UEFI)](V1_MILESTONE.md).\n\nThe goal is a **recognizable and maintainable port of the original SeaBIOS**,
not a separately developed firmware that happens to share its name.

- Latest technical status: [PHASE6_BOOTLIST_PE_RELOC.md](PHASE6_BOOTLIST_PE_RELOC.md)
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
```

A real Linux kernel has **not** been booted. The MBR/GPT test disks contain
synthetic ARM64 machine code, not Linux. Do not flash to actual ARM hardware.

The v0.8 legacy MBR/FAT32 and native PE32+ loader is not a Windows ARM64 loader, and supplies no UEFI services.
