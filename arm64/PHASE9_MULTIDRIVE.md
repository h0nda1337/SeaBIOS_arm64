# Phase 9 — native multi-drive registry and SeaBIOS geometry (v1.2-dev)

**AArch64 QEMU `virt`, experimental. No UEFI/EFI services. Not Windows.**

## Why this matters

AArch64 used an original SeaBIOS `drive_s` to dispatch `disk_op_s` to
VirtIO-MMIO, but previously had only one global queue/device. The v1.2 work
separates transport state per disk and uses the original x86 disk geometry
algorithm directly on AArch64, moving it out of `src/block.c`.

## Implementation

- `src/blockgeom.c`, `src/blockgeom.h`: original `setup_translation` CHS
  calculation factored from x86 `src/block.c`; both x86 and ARM64 use it.
  x86 still chooses its translation policy using RTC/CMOS and retains the
  original FDPT/BDA setup. ARM64 chooses LBA translation and treats CHS
  as internal drive metadata only (no x86 BIOS INT 13h interface).
- `src/hw/virtio-mmio.c`: ARM64 backend now holds up to four independent
  device contexts, with a separate split virtqueue, request header, status
  and transfer buffer. The x86 branch remains unchanged.
- `arm64/block.c`: indexes the actual SeaBIOS `drive_s`/`disk_op_s`
  structures and routes requests to the corresponding VirtIO transport.
- `arm64/diskprobe.c`: enumerates and reads each registered unit. For the
  synthetic test disks, it also checks a distinct diagnostic marker.
- Existing legacy MBR/FAT32, Linux Image and PE32+ diagnostic boot paths
  **continue to target disk index 0**. The presence of several disks is
  not multi-disk boot or a BIOS disk-number ABI.

## Verification

Run in the repository root on an x86-64 Linux host with Clang, LLD, an
ARM64-targeting compiler and `qemu-system-aarch64`:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-multidrive-test
make ARCH=arm64 qemu-bootorder-test
make ARCH=arm64 qemu-reloc-test
make
```

Observed locally in QEMU 10.0.13 with `-cpu cortex-a72` and 512 MiB:
- VirtIO-MMIO v1: 2 disks with capacities 4,096 and 6,144 sectors; both
  were registered and read successfully through their own queues.
- VirtIO-MMIO v2: same test passed.
- Host C geometry test: 13 assertions passed, compiling the exact same
  `src/blockgeom.c` used by x86 and ARM64.
- QEMU diagnostic boot (2/2), fw_cfg priorities (2/2), legacy MBR FAT32
  and native PE diagnostic boot sampled on each MMIO version: passed.
- PE32+ `DIR64` relocation positive/negative QEMU cases: passed (4/4).
- x86 built `out/bios.bin` (262,144 bytes).

The complete synthetic Linux disk test matrix exceeded the execution
time budget and **must not be counted as passed**. Genuine Linux 6.12
previously booted via v1.1 CI; v1.2 real-Linux regression is pending CI.

## Remaining work

Selection and booting from any registered drive, IRQ/GIC support, actual
PCI enumeration, write support, complex filesystems, full original POST,
legacy Windows ARM64 loader research and physical hardware portability.
Firmware remains read-only and should not be flashed to actual devices.
