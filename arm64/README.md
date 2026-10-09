# SeaBIOS-ARM64 — early AArch64 bootstrap (v0.1)

This is the **first independent AArch64 bring-up branch** placed alongside the
original SeaBIOS source tree. It does **not** implement legacy x86 BIOS, UEFI,
Windows on ARM boot, ACPI, PCI, or a general-purpose operating-system loader.
Do not flash it to physical hardware. It targets **QEMU `-machine virt` only**.
The x86 SeaBIOS Makefile and source files are deliberately unchanged.

## What works at source level

- AArch64 reset entry in QEMU flash (EL-dependent but MMU-off assumption), single
  primary CPU, interrupt masking, stack, .data relocation and .bss clearing.
- PL011 MMIO serial output with initial QEMU `virt` fallback address.
- Bounded FDT parser: validates the DTB header/tree and discovers root-level
  RAM and `arm,pl011` nodes; prints RAM and UART addresses.
- Optional **custom diagnostic** payload at physical `0x48000000`. The 64-byte
  header specifies `SBARMP01`, a length, and FNV-1a checksum. This is a
  development handoff, **not** a standard Linux boot protocol or a secure chain
  of trust. FNV-1a is not cryptographically secure.
- Freestanding cross-compilation with LLVM and host regression tests.

### Requirements

`clang` (AArch64 target), `ld.lld`, `llvm-objcopy`, `python3` and optionally
`qemu-system-aarch64`. LLVM is sufficient: no ARM GCC sysroot is required.

From the SeaBIOS repository root:

```bash
make -f arm64/Makefile
make -f arm64/Makefile test
make -f arm64/Makefile run
# With QEMU installed, verify both ROM-only and payload-handoff boot:
make -f arm64/Makefile qemu-test
```

Manual QEMU command (Linux/WSL):

```bash
qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 256M -smp 1 \
  -nographic -bios arm64/out/seabios-arm64.bin \
  -device loader,file=arm64/out/diagnostic-payload.bin,addr=0x48000000
```

Expected console (when tested on a QEMU version supporting these options):

```text
SeaBIOS-ARM64 bootstrap 0.1 (AArch64/QEMU virt)
[arm64] Device Tree bytes: ...
[arm64] PL011 UART: 0x0000000009000000
[arm64] RAM start: 0x0000000040000000 / size: 0x0000000010000000
[arm64] valid diagnostic payload -> branch 0x0000000048000040
[arm64] CHAINLOAD OK - diagnostic ARM64 payload running!
```

Quit QEMU with `Ctrl+A`, then `X`. Remove the `-device loader` option to test
booting to idle with no payload (this does not boot an OS).

## Assumptions and limitations

- The firmware is linked to address `0x00000000` in QEMU `virt` flash;
  RAM starts at `0x40000000`; a 2 MiB gap at the beginning of RAM is reserved
  for the DTB; firmware writable state is at `0x40200000`. These QEMU-specific
  constants must not be used as physical-platform addresses.
- QEMU's ARM documentation places the DTB at start of RAM for bare-metal
  firmware, not in `x0` (which applies to direct kernel boot).
- PL011 `0x09000000` is only an early-console fallback. The parser reads the
  UART base from DTB after startup. The DTB parser currently only handles
  direct root-child nodes and 1- or 2-cell addresses/sizes.
- The image is raw AArch64 code for QEMU flash, not an EDK2 FV, EFI binary,
  BIOS option ROM, or ARM32 firmware.
- The optional payload slot requires 256 MiB or more of guest RAM in this
  particular layout. It assumes physical identity mapping and MMU disabled.
- No secondary CPU/PSCI setup or exception vectors. QEMU execution still needs
  an **actual runtime test** before claiming successful boot if emulator is not
  available on the build machine.

## Next engineering milestones

1. Add QEMU CI/emulated boot regression test and harden FDT parser (including
   nested buses, ranges, memory banks and DTB relocation).
2. Implement architectural services: exception vectors, EL transitions, GIC,
   timer, PSCI, allocator and page tables.
3. Integrate portable SeaBIOS facilities incrementally behind clean interfaces
   while preserving original copyright and licensing. Original x86 C and INT
   services cannot be linked unchanged into this ARM64 build.
4. Add real boot protocols and storage (virtio-mmio/PCI and filesystems). Linux
   can use the AArch64 image/DTB protocol once prepared properly.
5. For **Windows ARM64**, add a conformant UEFI environment (normally leverage
   EDK2), ACPI, memory map, EFI boot/runtime services, EFI filesystem and ARM64
   drivers. A legacy SeaBIOS clone alone will not boot Windows ARM64.

This directory is a new, standalone platform target *inside the SeaBIOS tree*;
calling it a complete SeaBIOS port or Windows-capable firmware would be inaccurate.

## Licenses

New files in this ARM64 subtree carry `SPDX-License-Identifier:
LGPL-3.0-or-later`, following SeaBIOS's LGPLv3-or-later components. Existing
licenses and copyright notices in the unmodified SeaBIOS source are preserved.

## Verified in the build environment

- Compiled `seabios-arm64.elf`, raw `seabios-arm64.bin`, and the diagnostic
  payload using Clang 17/LLD/llvm-objcopy for AArch64.
- Validated ELF e_machine AArch64, entry address `0x0`, and ROM byte layout.
- 12 host-side regression tests succeeded.
- **QEMU was not installed in the build environment**; therefore live guest
  serial output/chainload is an expected result, not a measured result.
  Run `make -f arm64/Makefile qemu-test` in your Linux/WSL with QEMU installed.
