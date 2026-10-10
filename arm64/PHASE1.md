# Phase 1 — shared SeaBIOS services on AArch64 (v0.3-dev)

**Status: cross-compiled and QEMU smoke-tested (10.0.13). Experimental source, not production firmware.**

## Scope and rationale

This project is a SeaBIOS port, *not* a clean-room ARM firmware borrowing
its name. We retain the original x86 source tree, the original Makefile's
x86 route, and use real upstream SeaBIOS C implementations for subsystems
that can be shared without inventing a PC BIOS interface for ARM.

The parts already reused as compiled firmware components are:

| Original SeaBIOS source | AArch64 usage |
| --- | --- |
| `src/e820map.c` | Actual map insertion/overlap/splitting algorithm |
| `src/romfile.c` | Actual ROM-file registry, file lookup/load and typed values |
| `src/list.h` | Actual hlist primitives for current diagnostic boot candidates |
| `src/types.h` | Shared types, with AArch64 pointer-width conditionals |

This is *not* a port of the original `src/post.c`, `src/boot.c`,
`src/malloc.c`, or `src/output.c`. Their PC BIOS and 16/32-bit assumptions
remain separate, and their replacements in `arm64/` are transitional.

## ARM64 platform support added in this phase

- QEMU `virt` flattened Device Tree supplies RAM, PL011 UART, fw_cfg and
  up to 32 VirtIO MMIO transports. The memory map accepts up to eight RAM
  regions and 24 `/reserved-memory` spans. Explicitly disabled nodes are
  excluded before probing their MMIO ranges.
- The FDT memreserve table is parsed and clipped to the RAM range before it
  is inserted into the upstream E820 map. `/reserved-memory` child-node
  regions are also reserved. A malformed reservation table
  causes the firmware to refuse its diagnostic handoff.
- Real `src/romfile.c` now works with a temporary fixed-size allocator,
  freestanding strings and an ARM64 UART debug-formatting shim.
- The QEMU fw_cfg MMIO backend reads the signature and file directory,
  registers up to 48 small ROM files through `romfile_add()`, enumerates
  them via the **original** `romfile_findprefix()` and optionally reads
  `bootorder` via `romfile_loadfile()`. The DTB parser accepts the 10-byte
  MMIO window declared by QEMU (DMA is optional). It is a sequential
  polling backend, not a fw_cfg DMA driver.
- An early VirtIO-MMIO enumerator checks the standard magic, version,
  device and vendor ID registers. No virtqueue or block I/O is implemented.
- EL1/EL2 exception vectors log ESR/ELR/FAR over the QEMU virtual PL011.
  No exception recovery, GIC, ACPI or MMU is implemented yet.
- The demonstration loader checks that its fixed 1 MiB staging range is
  actually available according to the upstream memory map before accessing
  or entering the synthetic AArch64 payload, rather than relying on a
  single RAM bank.

## Build

Requires Clang with AArch64 target, LLD, llvm-objcopy, make and Python 3.
From the repository root:

```sh
make ARCH=arm64
# Equivalently: make -f arm64/Makefile
```

Firmware output: `arm64/out/seabios-arm64.bin`.
The legacy x86 build is still selected with the default `make` command.

Static source provenance can be inspected in the resulting ELF using:

```sh
nm arm64/out/seabios-arm64.elf | grep -E ' e820_add$| romfile_(add|findprefix|loadfile)$'
```

These functions are linked from the original `src/e820map.c` and
`src/romfile.c`, not renamed duplicate implementations in `arm64/`.

Validated on the development host with QEMU 10.0.13 (see [QEMU_VALIDATION.md](QEMU_VALIDATION.md)):

```sh
make ARCH=arm64 test
make ARCH=arm64 qemu-test
```

## AArch64 build fix verified in QEMU

The ARM64 CFLAGS include `-mgeneral-regs-only` and `-mstrict-align` to avoid
FP/SIMD operations before enabling the required CPU state and to prevent
unaligned memory accesses in the early freestanding firmware. This is an
initial compatibility workaround, not a replacement for platform-specific
CPU initialization. The existing x86 build flags are unchanged.

## Known limitations and unverified assumptions

1. QEMU 10.0.13 boot was smoke-tested on the `virt` machine (Cortex-A72, 512 MiB) and automated `qemu-test` checks passed for both no-payload and diagnostic-chainload paths. This does **not** establish portability to other QEMU versions, other ARM platforms, or OS bootability.
2. The E820 map is an *internal SeaBIOS data structure*; ARM64 does not expose
   the x86 INT 15h E820 ABI. Up to eight RAM banks and 24 reserved-memory
   regions are tracked, but generic bus `ranges` address translations and
   arbitrary Device Tree layouts are not yet supported.
3. Boot file sizes are limited by the fixed 64 KiB POST allocator. Its
   `free()` intentionally does not reclaim individual allocations. This is
   not a replacement for upstream `src/malloc.c`.
4. The diagnostic QEMU loader accepts a fixed custom header and FNV1a
   checksum; the latter is **not a cryptographic signature**. It does not
   boot a Linux Image, EFI binary or Windows on ARM.
5. The AArch64 vector tables diagnose fatal exceptions only. PSCI, GIC,
   timers, block storage, real boot priority and UEFI are still absent.
6. The ROM-file debug sink only covers the printf formats currently used by
   the shared modules. It is not a port of SeaBIOS's complete VGA/serial
   output subsystem.
7. All new code targets QEMU `virt` first. Do not flash to any physical
   ARM device, including the Moto G20.

## Planned Phase 2

Develop architecture-backed timer and IRQ services; add PCIe/virtio-mmio
transport, real storage discovery and a bootable standard AArch64 kernel
handoff; progressively refactor the shared SeaBIOS boot-priority logic from
`src/boot.c`. UEFI for Windows on ARM is a separate, much larger milestone.
