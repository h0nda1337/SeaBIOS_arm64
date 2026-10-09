# SeaBIOS-ARM64

Experimental AArch64 bootstrap for QEMU `virt`, developed alongside the original
[SeaBIOS](https://github.com/qemu/seabios) x86 codebase.

**Status: v0.1 — early bootstrap, NOT a complete BIOS port.**
It has an ARM64 entry point, PL011 serial console, limited FDT parsing
and a custom ARM64 diagnostic chainload. The AArch64 code is in
[`arm64/`](arm64/) and has build instructions in
[`arm64/README.md`](arm64/README.md). The original x86 sources are preserved.

Builds and host tests passed during development, but QEMU runtime boot
**has not been verified**. It does not boot Linux or Windows ARM64 and
must not be flashed to physical devices.

```sh
sudo apt-get install clang lld llvm qemu-system-arm python3 make
make -f arm64/Makefile
make -f arm64/Makefile test
make -f arm64/Makefile qemu-test
```

This first revision is intentionally platform-generic; it does not
integrate with MotoG20Pkg. Original licenses and notices are preserved
(see COPYING, COPYING.LESSER, and individual source headers).

Upstream SeaBIOS snapshot: `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`.
