# SeaBIOS-ARM64 — experimental port

The goal is to preserve the **identity of SeaBIOS** while adapting its
original internals to AArch64, not to replace it with unrelated firmware.

- Phase 1 architecture, shared upstream source and limitations:
  [PHASE1.md](PHASE1.md)
- Historical v0.2 integration notes: [PORTING.md](PORTING.md)
- Original SeaBIOS: [upstream](https://github.com/qemu/seabios)

## Build the AArch64 experimental firmware

```sh
make ARCH=arm64
```

Requires Clang, LLD, llvm-objcopy and Python 3. The build produces
`arm64/out/seabios-arm64.bin` and a synthetic diagnostic payload. If a
QEMU AArch64 system emulator is available, the existing `run` and `qemu-test`
Makefile targets can be used **after code review**. This revision was
cross-compiled but its runtime behavior has not been verified.

AArch64 firmware does **not** implement a legacy x86 BIOS ABI, Linux Image
boot, UEFI services, or Windows on ARM boot. The diagnostic payload is not
an operating system. Do not flash it to real hardware.
