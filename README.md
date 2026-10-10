# Coming soon... / Próximamente...

## SeaBIOS-ARM64

**An authentic port of SeaBIOS to AArch64 — not a new firmware merely inspired by it.**

The objective is to retain the original [SeaBIOS](https://github.com/qemu/seabios)
identity: its architecture, boot flow, recognizable C subsystems and original
x86 build wherever technically possible. ARM64-specific hardware interfaces
should be isolated behind architecture backends, not silently replace SeaBIOS.

### Experimental Phase 2 — v0.4-dev

The AArch64 firmware now cross-compiles and **executes** the original
SeaBIOS `src/e820map.c`, `src/romfile.c`, and `src/hw/virtio-ring.c`.
The original `src/hw/virtio-mmio.c` is now architecture-selective, preserving
its x86 implementation and providing an ARM64 backend for QEMU `virt`.

Read-only VirtIO block I/O supports legacy and modern MMIO. A limited loader
can find raw ARM64 Linux `Image` headers at the start of selected MBR or
validated GPT Linux partitions, copy them into bounded RAM, and transfer
control using the AArch64 Linux Image register convention. **The successful
runtime tests used synthetic ARM64 machine-code stubs, NOT a real Linux kernel.**

The original x86 `make` build still succeeds, while `make ARCH=arm64` builds
the experimental AArch64 target. Tests were run against QEMU 10.0.13.

Source, build instructions, results and limitations: [Phase 2](arm64/PHASE2.md).

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
```

**Important:** SeaBIOS-ARM64 is not a full BIOS port. There is no complete
SeaBIOS POST, full boot manager, filesystem loader, UEFI, Windows ARM64 boot,
or validated real Linux distribution boot. Never flash it to a physical device.

### Español

**El objetivo es portar SeaBIOS de verdad a ARM64, conservando su identidad
original.** No buscamos desarrollar otro firmware que simplemente utilice su
nombre. La primera etapa ya enlaza módulos originales de SeaBIOS en AArch64,
pero aún no implementa su POST completo, el arranque de sistemas operativos ni UEFI.

**Estado:** v0.4-dev, compilación ARM64 y x86 verificadas; lectura de
sectores y salto a código ARM64 sintético probados en QEMU 10.0.13.
Todavía no se ha arrancado un kernel Linux auténtico ni Windows ARM64.
Consulta [PHASE2.md](arm64/PHASE2.md) para conocer las limitaciones.

---

Upstream SeaBIOS snapshot: `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`.
Original licenses and third-party notices are retained in the repository.
