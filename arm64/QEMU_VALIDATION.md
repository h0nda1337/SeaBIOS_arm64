# QEMU AArch64 validation — phase 1 v0.3-dev

**Status:** QEMU smoke-tested on 2026-10-10. This is NOT a Linux/Windows boot test and NOT a finished BIOS or UEFI implementation.

## Changes required for runtime execution

The AArch64 freestanding C compiler flags in `arm64/Makefile` now include:

- `-mgeneral-regs-only`: avoid compiler-generated FP/SIMD instructions before their execution context has been initialized.
- `-mstrict-align`: avoid compiler-generated unaligned accesses on early firmware paths.

These flags apply to the AArch64 build only, not to upstream SeaBIOS x86.

## Reproduction

Dependencies: Clang with AArch64 target, LLD, llvm-objcopy, make, Python 3 and `qemu-system-aarch64`.

Run from the root of the repository:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
```

The `qemu-test` target uses the `virt` machine, `cortex-a57`, 256 MiB RAM, PL011 serial and a five-second timeout per case. It verifies that expected serial messages appear in two independent scenarios: with no payload and with a diagnostic payload at `0x48000000`. The emulator is terminated by the test harness; a timeout is not necessarily a firmware failure.

## Observed results

Development host: Debian x86_64, QEMU 10.0.13 using TCG.

- AArch64 firmware and diagnostic payload built and linked successfully.
- Python host tests: 10 FDT tests + 4 payload tests, all passing.
- QEMU with no payload: **PASS** (early firmware / idle diagnostic).
- QEMU with synthetic diagnostic payload: **PASS**, including `CHAINLOAD OK`.
- The firmware was also observed printing AArch64 EL1, RAM map, fw_cfg ROM-file registry and VirtIO-MMIO discovery on QEMU `virt` with `cortex-a72` and 512 MiB RAM.

Selected observed UART output:

```text
SeaBIOS-ARM64 phase 1 v0.3-dev (AArch64/QEMU virt)
[cpu] AArch64 exception level: 1
[post] SeaBIOS memory map (src/e820map.c)
[fwcfg] registered romfiles: 6
[virtio] MMIO transports in DTB: 32
[boot] probing: AArch64 QEMU diagnostic payload
[arm64] valid diagnostic payload -> branch
[arm64] CHAINLOAD OK - diagnostic ARM64 payload running!
```

Local built `arm64/out/seabios-arm64.bin` on this verification:
- Size: 24,208 bytes
- SHA-256: `b215678d435403807995dc1a33674f6813d14b27e27bd040815efd868373559b`

## Scope and limitations

The chainloaded image is a custom synthetic AArch64 diagnostic payload, not an OS kernel. The POST logic is transitional and relies on a subset of genuine upstream SeaBIOS services (`e820map`, `romfile`, list primitives). No functioning VirtIO block storage, Linux Image boot, UEFI services, Windows ARM boot, or physical ARM board support is established by these tests.

Do not flash this experimental firmware to a real phone or other physical device.
