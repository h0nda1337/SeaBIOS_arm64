# SeaBIOS-ARM64 v1.0-experimental — first real OS userspace on AArch64

## Why this version merits v1.0

For the first time, the SeaBIOS-ARM64 firmware booted **an authentic, unmodified
Debian Linux AArch64 kernel** from a **legacy MBR** disk and successfully reached
a native Linux userspace executable (`/init`). No UEFI, no EFI System Partition
and no EFI services were used at any point.

This was not our older synthetic test binary pretending to have a Linux header.
The tested kernel was Debian Linux **6.12.111** (cloud ARM64 variant),
extracted from `linux-image-6.12.111+deb13-cloud-arm64-unsigned` and loaded
as a genuine uncompressed ARM64 `Image`. Its header declared 31,916,032 bytes,
and the actual Image file contained 30,892,544 bytes.

### Actual firmware boot path

1. Our SeaBIOS-derived AArch64 firmware starts on QEMU `virt`, EL1.
2. Original SeaBIOS memory-map / block / ROM-file infrastructure participates
   in AArch64 POST and VirtIO block discovery.
3. The firmware reads an **MBR** type 0x83 raw kernel partition from a VirtIO
   disk and loads the real Linux Image into ARM64 RAM.
4. The optional type 0xDA raw initramfs partition supplies a tiny Linux AArch64
   ELF `/init`; this partition convention is specific to this experimental
   bootloader, not a generic Linux distribution standard.
5. The firmware rewrites Device Tree `/chosen` with kernel command line and
   initrd bounds and performs the non-EFI Linux Image register handoff.
6. The unmodified Linux kernel prints its genuine boot log and runs PID 1:
   `Run /init as init process`, followed by a userspace marker.

### Evidence — successful GitHub Actions execution

- **Both VirtIO-MMIO v1 (legacy) and v2 (modern): PASS.**
- [Verified CI run #38109370998](https://github.com/h0nda1337/SeaBIOS_arm64/actions/runs/38109370998)
- [Standalone kernel test workflow](../.github/workflows/real-linux-smoke.yml)
- Logs are published as a GitHub Actions artifact on the run.

The workflow fetches the genuine Debian kernel, creates a raw MBR disk, builds
a tiny userspace initramfs and boots our own firmware using QEMU `-bios`.
It checks for both the `Linux version` banner and the exact userspace marker.
The first historical test had a truncated diagnostic string (not a kernel
failure); the corrected run succeeded and the expanded two-transport test
also passed.

## Important limits

- This is **not a complete native port of SeaBIOS's x86 runtime/POST**.
- It does **not boot Windows ARM64**, which has no standard BIOS legacy
  boot pathway; no Windows loader integration is claimed.
- We have **not** booted a general Linux distribution from a FAT/ext4 root
  filesystem. The tested boot medium uses raw MBR partitions.
- Device support remains tailored to QEMU `virt`, with polling read-only
  VirtIO block I/O and limited firmware services.
- Booting a Linux kernel is not evidence of support for real ARM phone hardware.
- Experimental code must not be flashed to the Moto G20.

This marks a verifiable OS-boot milestone and is a foundation for further
SeaBIOS core reuse; it must not be interpreted as 100% port completion.
