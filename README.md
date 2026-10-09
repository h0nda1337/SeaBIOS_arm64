# Coming soon... / Próximamente...

## SeaBIOS-ARM64

**A true port of SeaBIOS to AArch64 — not a new firmware merely inspired by it.**

The long-term goal of this project is to bring the original [SeaBIOS](https://github.com/qemu/seabios) to ARM64 **without losing the identity of SeaBIOS**. We intend to preserve its recognizable architecture, boot flow, core subsystems, coding conventions, and as much of its original code as technically possible.

An engineer inspecting this repository should be able to recognize **SeaBIOS itself**, not simply another bootloader using the same name.

The port will aim to:
- Reuse and adapt original SeaBIOS core code rather than replace it with an unrelated implementation.
- Isolate unavoidable x86/AArch64 differences behind architecture-specific interfaces.
- Preserve the original x86 build and behavior as ARM64 support develops.
- Integrate AArch64 into the existing project structure and build system progressively.
- Begin with platform-generic QEMU `virt` support; consider additional ARM platforms later.

**Technical boundary:** AArch64 has no native x86 real mode or BIOS `INT` ABI. Exact x86 BIOS compatibility would require emulation. Any future Windows on ARM support would need a compatible UEFI interface; it is **a long-term research goal, not an existing feature**.

### Current state — v0.1

The first published revision is an **experimental AArch64 bootstrap**, not yet a complete SeaBIOS port. It contains an ARM64 entry point, PL011 serial output, basic Device Tree parsing, and a diagnostic chainload path. Build and host tests passed during development; **runtime boot under QEMU has not yet been verified**.

- Source: [`arm64/`](arm64/)
- Build and testing: [`arm64/README.md`](arm64/README.md)
- Existing SeaBIOS x86 source and license notices are retained.
- It **does not** currently boot Linux or Windows ARM64, and must **not** be flashed onto physical devices.

### Español

**El objetivo es portar SeaBIOS de verdad a ARM64, sin perder su identidad original.** No buscamos desarrollar otro firmware inspirado en SeaBIOS ni sustituir progresivamente su código por un proyecto distinto.

Queremos conservar su arquitectura interna, flujo de arranque, subsistemas y la mayor cantidad viable de código original. Las diferencias inevitables de AArch64 se implementarán en capas específicas de arquitectura, mientras la versión x86 original continúa disponible.

**Estado actual:** la v0.1 es solo una base de arranque experimental para AArch64. Todavía no es un port completo, no se ha confirmado su arranque en QEMU y no inicia Linux ni Windows ARM64.

---

Original SeaBIOS: https://github.com/qemu/seabios  
Upstream source snapshot: `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`  
Licenses and third-party notices remain in their original files.
