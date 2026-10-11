# SeaBIOS-ARM64 — Phase 2 (v0.4-dev)

**Status:** Experimental AArch64 firmware. Cross-compiled and tested under QEMU,
NOT yet proven to boot a real Linux kernel, and NOT a full BIOS or UEFI port.
This implementation must **not** be flashed to phones or other real devices.

## Identity: original SeaBIOS modules executed on ARM64

The AArch64 build compiles and executes original SeaBIOS code, retaining the
standard x86 build (`make`) and adding a separate opt-in `make ARCH=arm64`:

| Shared upstream source | What is used on ARM64 |
| --- | --- |
| `src/e820map.c` | Original region splitting and overlap calculations |
| `src/romfile.c` | Real SeaBIOS ROM-file registry and loader |
| `src/hw/virtio-ring.c` | Original split-ring enqueue/kick/used/reclaim algorithms |
| `src/hw/virtio-mmio.c` | Platform split: existing x86 driver and new ARM64 MMIO transport in one source file |
| `src/hw/virtio-ring.h`, `src/hw/virtio-blk.h` | Shared queue structures and request headers |
| `src/list.h` | Linked-list boot-target infrastructure |

`src/hw/virtio-ring-common.h` extracts architecture-neutral declarations from
the original SeaBIOS ring header. The x86 build path remains intact and was
compiled successfully during this phase (using native GCC toolchain).

**Still architecture-specific:** `arm64/post.c`, `arm64/bootorder.c`, the
freestanding allocator, UART, FDT, timer and raw Linux Image loader. They
are transitional components; the original x86 `src/post.c` and `src/boot.c`
are not yet compiled or executed on ARM64.

## Tested milestones

1. ARM64 EL1 entry, exception vector diagnostics, PL011, generic timer
   (`CNTFRQ_EL0`/`CNTVCT_EL0`), and FDT memory reservations on QEMU `virt`.
2. Working read-only VirtIO-MMIO block I/O through *both* version 1 (legacy)
   and version 2 (modern) transports. Reads can transfer up to 16 sectors per
   request; a 32-sector synthetic Image has its final byte verified after
   two successive requests using the original SeaBIOS split-ring code.
3. MBR: active Linux type 0x83 partition (or first matching fallback).
4. GPT: primary header CRC32 + partition-array CRC32 are checked; first
   Linux filesystem GUID (`0fc63daf-8483-4772-8e79-3d69d8477de4`)
   with valid LBA bounds can be used as a **raw Image test candidate**.
5. Basic Linux ARM64 `Image` header validation (magic `ARM\x64`, size,
   endianness and placement) and copying up to 64 MiB into reserved-checked RAM.
6. AArch64 handoff with DTB in `x0`, other parameters cleared, interrupts
   masked, data cache cleaned to PoC with `dc cvac` over loaded code and
   instruction cache invalidated before branching.
7. Tested on actual QEMU 10.0.13 using a small **synthetic AArch64 executable**
   bearing an ARM64 Linux Image header. It prints a diagnostic message. It
   is **not Linux** and does not establish Linux kernel boot support.

## Run tests

Requires Clang (AArch64 target), LLD, LLVM objcopy, Python 3, a host C
compiler, make, and `qemu-system-aarch64`:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
```

The `qemu-disk-test` target creates read-only synthetic MBR/GPT disk images in
`arm64/out/` and tests the legacy and modern VirtIO transports. The raw Image
stub is generated entirely from the source in `arm64/tests/`.

Expected messages for synthetic Image:

```text
SeaBIOS-ARM64 phase 2 v0.4-dev (AArch64/QEMU virt)
[timer] CNTFRQ_EL0: 62500000 Hz
[virtio-blk] ready, sectors: ...
[linux] supported ARM64 Image candidate at LBA ...
[linux] Image loaded; entering AArch64 Image entry
[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux)
```

The test runner kills QEMU when firmware yields (`WFE`). This is expected,
not a sign that a real OS returned successfully.

## Deliberate restrictions

- **No Linux distribution boot has been verified.** The loader has no initrd,
  kernel command-line provisioning, filesystem parser, dynamic memory
  placement, actual OS boot test, or complete CPU/platform initialization.
- Only **uncompressed**, little-endian AArch64 `Image` headers with nonzero
  image size (512 bytes–64 MiB) are accepted. The loader expects the `Image`
  immediately at the start of a Linux MBR/GPT partition and ignores a
  conventional ext4/FAT filesystem in that partition.
- The GPT implementation validates only bounded primary tables with exactly
  128-byte entries and counts divisible by four; backup GPT is not implemented.
- Block I/O is **read-only**, single request in flight (up to 16 sectors / 8 KiB per read), physical-address
  identity mapping, no IOMMU, no cache-coherence abstraction for hardware
  outside QEMU, bounded polling rather than interrupts, and one device.
- The existing POST and boot-manager compatibility shims are *not* the
  original full 16-bit/32-bit SeaBIOS x86 BIOS runtime.
- No UEFI services, ACPI generation for ARM platforms, EDK2 replacement,
  Windows ARM64 boot, secure boot, storage filesystem, or MotoG20Pkg integration.

## Phase 3 direction

(1) Validate handoff with a real Linux AArch64 kernel and initramfs;
(2) build an interrupt/GIC path and better physical memory allocator;
(3) share more of the original SeaBIOS block/boot priority layer;
(4) add an actual filesystem loader and platform-neutral boot chain.
Preserve the original SeaBIOS code and default x86 build at every step.
