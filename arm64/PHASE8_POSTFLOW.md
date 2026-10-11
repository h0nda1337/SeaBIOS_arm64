# Phase 8 — genuine SeaBIOS POST sequencing shared with ARM64

**v1.1 experimental. Not a complete ARM64 SeaBIOS runtime. No UEFI.**

## Change from v1.0

The x86 SeaBIOS POST used to sequence its hardware and interface setup
within `src/post.c::maininit()`. The same ten-stage order is now implemented
by **`src/postflow.c::postflow_run()`**, which is compiled into *both* the
original x86 BIOS and the native AArch64 firmware.

The x86 `maininit()` invokes that function with callbacks that preserve the
original ordering and original operations (IVT/BDA, PIC, timers, VGA ROM,
option ROMs, boot menu, `prepareboot()`, BIOS readonly, INT19). The ARM64
boot path invokes **the same function** after validating the QEMU Device Tree,
using native callbacks for E820-style internal RAM map, fw_cfg/ROM files,
VirtIO-MMIO device discovery, legacy MBR/Linux targets, and handoff.

| Stage | x86 backend | ARM64 backend |
| --- | --- | --- |
| Interface | malloc, ROM files, IVT, BDA | FDT memory reservations + original E820 map |
| Platform | PIC, QEMU/coreboot, clock, TPM | MMIO fw_cfg + original romfile |
| Async devices | Threads, if configured | Intentionally omitted (no IRQ scheduler) |
| Video | VGA/serial and option ROM | PL011 already initialized in early setup |
| Sync devices | USB/PS2/block/serial | VirtIO-MMIO block discovery, MBR/GPT probing |
| Option ROMs | Execute x86 option ROMs | Intentionally omitted |
| Boot menu | Interactive BIOS menu | Register ARM64 diagnostic fallback |
| Prepare | BCV, finalize BIOS tables | Loaders check and reserve ranges |
| Protect | Make BIOS read-only | Intentionally omitted (MMU/flash protection TODO) |
| Boot | x86 INT 19h | Original shared IPL policy + native ARM64 handoff |

The new `postflow_run()` supports *fail-fast*: any failed callback aborts
without entering later stages, and it reports the failed stage to the caller.
The independent C host test compiles the **same `src/postflow.c`** and checks
stage order, all 10 failure boundaries, and invalid operation callbacks.

## Verification performed locally

- AArch64 firmware compiled successfully (Clang + LLD).
- Original SeaBIOS x86 compiled successfully; `out/bios.bin` remains 256 KiB.
- ARM64 QEMU 10.0.13 `virt`: diagnostic no-payload and payload passed.
- QEMU bootorder with VirtIO-MMIO v1 and v2 passed.
- Selected legacy MBR/FAT32 and SHA-256 corruption scenarios passed.
- Entire host test suite passed, including the new POST sequencing test.
- Legacy 34-case QEMU suite was interrupted by a test execution time limit,
  so its last result is **not** counted as complete (tested separately).

## Still missing

This does **not** execute x86 `src/post.c` literally on ARM64, implement
16-bit BIOS calls, port VGA/PCI/USB/PS2 BIOS hardware initialization, provide
Windows ARM64 boot, or offer UEFI. A full x86 runtime regression test is also
pending (the x86 build, not the x86 runtime, was verified).

The previously demonstrated Linux MBR boot can be checked again on CI with
the new `arm64-v11-verify.yml` workflow, which fetches an authentic ARM64
Linux Image rather than relying on synthetic code.
