# Coming soon... / Próximamente...

## SeaBIOS-ARM64

**An authentic port of SeaBIOS to AArch64 — not a new firmware merely inspired by it.**

The objective is to retain the original [SeaBIOS](https://github.com/qemu/seabios)
identity: its architecture, boot flow, recognizable C subsystems and original
x86 build wherever technically possible. ARM64-specific hardware interfaces
should be isolated behind architecture backends, not silently replace SeaBIOS.

### Experimental Phase 1 — v0.3-dev

This branch now cross-compiles the genuine SeaBIOS `src/e820map.c` and
`src/romfile.c` implementations into an AArch64 firmware for QEMU `virt`.
It also reuses `src/list.h`, detects RAM and reserved memory through FDT,
implements a PL011 diagnostic console, discovers VirtIO-MMIO transport slots
and accesses QEMU firmware files through an ARM64 `fw_cfg` MMIO backend.

**Important:** This is not yet a complete SeaBIOS port. The original POST,
boot manager, storage drivers and x86 BIOS interrupt ABI have not been ported.
There is no UEFI implementation, and Linux/Windows ARM64 cannot boot yet.
Compilation succeeded, but **QEMU runtime and unit tests were not run**.
Do not flash this experimental firmware to physical hardware.

Source and design notes: [arm64/](arm64/) · [Phase 1 details](arm64/PHASE1.md)

```sh
make ARCH=arm64
# firmware output: arm64/out/seabios-arm64.bin
```

The original x86 build remains the default when invoking `make` without `ARCH`.
Windows on ARM is a **long-term research objective** and would require a
compatible UEFI environment. x86 machine-code compatibility would additionally
require translation or emulation.

### Español

**El objetivo es portar SeaBIOS de verdad a ARM64, conservando su identidad
original.** No buscamos desarrollar otro firmware que simplemente utilice su
nombre. La primera etapa ya enlaza módulos originales de SeaBIOS en AArch64,
pero aún no implementa su POST completo, el arranque de sistemas operativos ni UEFI.

**Estado:** v0.3-dev, compilación cruzada completada; arranque en QEMU no
verificado. Consulta [PHASE1.md](arm64/PHASE1.md) para las limitaciones técnicas.

---

Upstream SeaBIOS snapshot: `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`.
Original licenses and third-party notices are retained in the repository.
