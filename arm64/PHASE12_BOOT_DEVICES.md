# Phase 12: per-drive boot selection (v1.3-dev; no UEFI)

This is an experimental SeaBIOS AArch64 port. It is **not** a PC x86 BIOS
implementation on ARM, does not run original x86 MBR/VBR binaries, and does not
boot Windows on ARM64. The goal is to reuse SeaBIOS's real block/boot-priority
subsystems and support native ARM64 OS loading without EFI.

## Changes relative to v1.2

1. The registered SeaBIOS `drive_s` entries remain per VirtIO-MMIO transport,
   but **boot candidates now also retain their source disk index**.
2. The SeaBIOS `fw_cfg` `bootorder` parser and IPL sorting policy can distinguish
   all supported disks by their QEMU `/virtio-mmio@.../*` firmware path;
   `bootindex` can rank different disks independently.
3. `arm64_legacy_discover()` scans each disk for an active MBR FAT32 volume
   and `/BOOT/ARM64.BIN` or `/BOOT/ARM64.EXE`. Its candidate stores a snapshot of
   FAT geometry, file size and cluster to prevent the next drive's probe from
   overwriting its state. FAT cache is invalidated when an attempt switches disks.
4. `arm64_linux_discover()` scans every drive for raw ARM64 `Image` on MBR or
   GPT, with an optional raw initrd on the **same disk**. Boot attempts use
   `arm64_virtio_blk_read_many_at(index, ...)` exclusively.
5. `arm64_gpt_find_linux_at()` validates GPT (including the partition array
   CRC) on a designated drive. The original disk-0 API is maintained.
6. Kernel and initrd E820-like reservations are committed after all disk I/O
   and FDT preparation has succeeded, so an unsuccessful attempt cannot block
   a later candidate purely by pre-reserving the common load address.

## Local QEMU verification

Use an ARM64-capable Clang/LLD and `qemu-system-aarch64`:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-multiboot-test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-bootorder-test
make ARCH=arm64 qemu-reloc-test
make -j2  # Original x86 SeaBIOS
```

The new multi-boot test runs ten scenarios across VirtIO MMIO v1/v2:
* A valid FAT32 native ARM64 second-stage on disk **1**; disk **0** blank.
* Two FAT32 drives with alternate QEMU `bootindex` ordering in both directions.
* Synthetic Linux ARM64 `Image` on disk **1** from MBR and GPT.

All test stages above are synthetic ARM64 code. The separate `real-linux-mbr`
CI job takes a *genuine Debian Linux ARM64 kernel*, adds a small initramfs, and
attempts to reach `/init` with disk 0 blank and Linux on disk 1. Its success
requires a **separate CI execution**; unit tests alone do not prove it.

## Deliberate scope

- QEMU `virt` with up to four read-only VirtIO-MMIO drives and one outstanding
  transfer per drive; no PCIe, NVMe, USB, AHCI, network, or real-board support.
- Raw MBR/GPT partition and restricted FAT32 file loading; not a general
  FAT32/ext4 driver, full boot menu or automatic OS installation support.
- Original SeaBIOS `bootprio`, `blockpolicy`, `blockgeom`, `romfile`, `e820map`
  and VirtIO ring code shared; original POST and runtime still incomplete.
- Neither PE32+ support nor MBR boot implies compatibility with Microsoft's
  Windows Boot Manager, `winload`, NT HAL, ACPI expectations or Windows ARM64.
- No UEFI services or EFI loading are introduced.
