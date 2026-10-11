# SeaBIOS-ARM64 v0.5-dev — experimental Linux boot protocol preparation

**Scope:** QEMU `virt` AArch64 only. This build is a firmware experiment, NOT
proof of booting a real Linux kernel, Windows ARM64, or a physical ARM board.

## New code

* `arm64/fdt_boot.c` is a bounded, freestanding FDT v17 writer. It preserves
  existing device nodes, string references, reserved-memory entries, and
  existing `/chosen` child nodes while replacing the bootloader-provided
  `bootargs`, `linux,initrd-start` and `linux,initrd-end` properties. The
  regenerated DTB lives in reserved private RAM and is cleaned to PoC.
* `arm64/linuxboot.c` can load an optional **raw** initrd from a designated
  second MBR partition (experimental type `0xda`, up to 16 MiB), align it
  beyond the kernel Image, reserve it in the SeaBIOS E820-derived memory map
  and advertise its location via the new FDT.
* Empty, out-of-disk, oversized and overlapping initrd partitions are rejected.
  The bootloader does not parse cpio or mount filesystems.
* The freestanding BSS-zeroing loop writes 16 bytes per iteration to allow
  the enlarged firmware-private FDT scratch space. The private RAM window
  is increased from 2 MiB to 4 MiB on QEMU `virt`.
* The synthetic Linux Image checks the FDT magic passed in `x0` before
  accepting the handoff. It is still a diagnostic executable, not Linux.
* The Python QEMU test runners use process groups to bound emulator lifetime,
  reducing the risk of orphaned TCG processes.

## Reproduction

From repository root, with Clang/LLVM, GCC and `qemu-system-aarch64`:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
```

The host FDT writer tests use a small synthetic FDT, covering an existing
`/chosen`, a missing `/chosen`, nested children, initrd pointers, duplicates,
root cell size mismatches and malformed inputs. The writer was also exercised
against QEMU's own dumped DTB (1 MiB allocation) and the output independently
parsed on the host. The test disk is **not a Linux filesystem**.

## Expected synthetic UART output (when --initrd test image is attached)

```text
[virtio-blk] ready
[linux] supported ARM64 Image candidate
[initrd] raw partition loaded at 0x0000000048200000
[fdt] prepared /chosen for Linux
[linux] SYNTHETIC IMAGE HANDOFF OK (not Linux; tail verified)
```

## Roadmap / hard limits

1. **Actual Linux kernel boot is NOT proven.** An uncompressed Image, real
   initramfs with `/init`, appropriate memory size and a suitable kernel config
   must be tested separately. The loader is not a UEFI boot manager.
2. The `bootargs` line is currently fixed to `console=ttyAMA0,115200`
   and `rdinit=/init`. General command-line configuration is pending.
3. The direct-to-disk Image and initrd partitions are **private experimental
   layouts**, not a standard distro partition or installed OS boot method.
   GPT supports the raw kernel candidate but not the auxiliary initrd layout.
4. The FDT writer requires version >= 17, a normal layout with strings after
   the structure block, root `#address-cells = 2`, and at most 2 MiB input.
   It does not attempt generic overlay or OFW support.
5. VirtIO I/O is read-only/polling, no SATA/NVMe, no interrupts/GIC, no
   platform-independent CPU initialization. Legacy SeaBIOS x86 BIOS services,
   original POST/boot.c, full firmware PCI scan and UEFI services remain unported.

## Progress percentages

These are **subjective engineering estimates**, not automated code coverage:
- Early QEMU ARM64 platform bring-up: ~65% (console, FDT, basic RAM map,
  fw_cfg, VirtIO block, but no GIC/PSCI bring-up beyond QEMU support).
- Experimental direct Linux ARM64 handoff: ~40% (synthetic test only;
  actual kernel/initramfs unverified).
- Faithful full SeaBIOS ARM64 port: **~10-15%**. Most original SeaBIOS core
  and BIOS services are not yet portable. Only a few original modules
  are currently reused directly.
- Boot Windows ARM64 on this firmware: **<5%**. It requires a compatible
  UEFI implementation, ACPI, firmware runtime services and Windows drivers.

We prioritize fidelity to SeaBIOS by sharing original `e820map.c`, `romfile.c`
and `virtio-ring.c` rather than rewriting their internals. The original x86
build is kept functional. No claim of a complete ARM64 port is warranted.
