# Coming soon... / Próximamente...

## SeaBIOS-ARM64

### v1.3-dev — independent native ARM64 boot devices (No UEFI)

The AArch64 boot manager now keeps each VirtIO-MMIO disk's own firmware
pathname, MBR/GPT partition data and FAT32 boot-file state. It loads native
ARM64 stages or a raw Linux Image from **disk 1 even if disk 0 is empty**.
The existing shared SeaBIOS `bootorder` logic handles distinct per-device
QEMU `bootindex` priorities. Ten local QEMU smoke scenarios passed on
VirtIO-MMIO v1 and v2 (including synthetic Linux MBR/GPT handoff).

[Technical details](arm64/PHASE12_BOOT_DEVICES.md) ·
[New multidisk tests](arm64/tests/test_qemu_multiboot.py).

CI additionally attempts a *genuine Debian ARM64 Linux kernel* from MBR on
the second disk (with the first disk unbootable); its outcome must be checked
from the workflow logs. No UEFI or Windows ARM64 loader has been implemented.

### v1.2-dev — multi-drive SeaBIOS registry, shared x86/ARM64 CHS geometry

AArch64 now registers up to **four** independent read-only VirtIO-MMIO drives,
each backed by a separate split ring and an original SeaBIOS `drive_s`.
The actual geometry translation algorithm from `src/block.c` lives in
`src/blockgeom.c` and is compiled for **both x86 and ARM64**. QEMU smoke
tests verified two distinct drives under VirtIO-MMIO v1 and v2, and the
original x86 256 KiB BIOS still compiles.

The preexisting MBR/FAT32 and raw Linux Image boot paths still use **disk 0**.
Recognizing multiple drives does **not** yet mean booting arbitrary drives.
No UEFI or Windows ARM64 boot has been implemented. See
[Phase 9 — multiple drives](arm64/PHASE9_MULTIDRIVE.md).


### v1.0 experimental — real Linux ARM64 boot from legacy MBR, no UEFI

**Milestone achieved:** our native AArch64 firmware loads an authentic Debian
Linux 6.12 ARM64 kernel from a raw MBR partition via SeaBIOS-style block I/O,
prepares an initramfs and Device Tree, and successfully runs Linux userspace
`/init` in QEMU. Verified on **both legacy and modern VirtIO-MMIO** transports.
See [v1.0 milestone documentation](arm64/V1_MILESTONE.md) and the
[successful dual-mode GitHub Actions run](https://github.com/h0nda1337/SeaBIOS_arm64/actions/runs/38109370998).

**No UEFI and no Windows ARM64 boot.** This is a major experimental milestone,
not a full implementation of the original SeaBIOS POST/runtime ABI.


**An authentic port of SeaBIOS to AArch64 — not a new firmware merely inspired by it.**

The objective is to retain the original [SeaBIOS](https://github.com/qemu/seabios)
identity: its architecture, boot flow, recognizable C subsystems and original
x86 build wherever technically possible. ARM64-specific hardware interfaces
should be isolated behind architecture backends, not silently replace SeaBIOS.

### Shared SeaBIOS core phase — v0.7-dev (No UEFI)

The original boot-order glob matcher from `src/boot.c` now lives in the shared
`src/bootprio.c` implementation built by both SeaBIOS x86 and AArch64.
The AArch64 loader honors QEMU's `fw_cfg` VirtIO-MMIO boot priority as a
real `bootorder` entry. The original `src/sha256.c` also builds on AArch64,
with an optional SHA-256 integrity-checked native MBR stage-2 container
(`SBARMLE2`; old `SBARMLE1` remains supported). SHA-256 does not authenticate
untrusted disk contents. Synthetic payloads still do not boot Windows ARM64.

See [Phase 5 — shared source](arm64/PHASE5_SHARED_CORE.md).

### v0.9-dev — Shared SeaBIOS block operations (No UEFI)

ARM64 now registers a real SeaBIOS `drive_s` and handles `disk_op_s`
operations before calling the VirtIO-MMIO transport. Shared policy
`src/blockpolicy.c` factors the original SeaBIOS 64-KiB transfer rule,
partial-read accounting, and default disk-command handling out of
`src/block.c`; these functions are compiled for both x86 and ARM64.
The x86 BIOS still builds, and selected ARM64 legacy MBR/FAT32 and
PE32+ diagnostic stages run on QEMU VirtIO v1 and v2.

**This is not a complete POST, does not boot Windows ARM64, and adds no UEFI.**
See [Phase 7 — block core](arm64/PHASE7_BLOCK_CORE.md).

### Experimental v0.8 — Shared IPL ordering and relocated native PE (No UEFI)

SeaBIOS's original IPL candidate-ordering policy is now factored into
`src/bootprio.c` and called by **both x86 and AArch64**. On ARM64, the
legacy MBR/FAT32 loader supports bounded `IMAGE_REL_BASED_DIR64`
relocations of import-free **native-subsystem** PE32+ ARM64 stages.
The rebased diagnostic stage passed QEMU VirtIO-MMIO v1/v2 positive and
corrupt-relocation rejection tests; x86 still builds its 256 KiB BIOS.

This is **not** a Windows ARM64 loader: no genuine Windows boot
components or real Linux kernel were booted, and there is **no UEFI**.
See [Phase 6](arm64/PHASE6_BOOTLIST_PE_RELOC.md).

### Experimental legacy MBR phase — v0.6-dev (No UEFI)

This branch adds a native AArch64 BIOS-like boot path: **active MBR
FAT32 → `/BOOT/ARM64.BIN` or `/BOOT/ARM64.EXE` → native ARM64 second stage**.
It can load both a custom verified flat container and a strictly limited
**PE32+ ARM64** image with *native* subsystem, no imports and a fixed image
base. It rejects EFI-subsystem binaries rather than pretending to execute
them. Both native payload formats execute in QEMU; 30 legacy-disk QEMU
scenarios, including malformed inputs, pass on VirtIO-MMIO v1 and v2.

**This does not boot Windows ARM64.** Microsoft's ARM/SoC Windows boot
contracts require more than MBR, FAT32 or parsing a PE file. This project
intentionally does not implement UEFI; a Windows ARM64 legacy loader would
require substantial new, separately researched NT boot support. See
[Phase 4 — legacy MBR](arm64/PHASE4_LEGACY_MBR.md).

### Experimental Phase 3 — v0.5-dev

The experimental AArch64 loader now reconstructs the FDT `/chosen` node,
provides a fixed kernel command line, and can pass an optional raw MBR initrd
from a nonstandard dedicated partition. QEMU legacy and modern VirtIO-MMIO
regression tests exercise these paths, including corrupt/overlapping input.
The synthetic Image now rejects an invalid DTB pointer at handoff.

**This does not demonstrate a real Linux kernel boot.** More details:
[Phase 3](arm64/PHASE3.md).

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
SeaBIOS POST, full boot manager, general-purpose filesystem loader, UEFI, Windows ARM64 boot,
or validated real Linux distribution boot. Never flash it to a physical device.

### Español

**El objetivo es portar SeaBIOS de verdad a ARM64, conservando su identidad
original.** No buscamos desarrollar otro firmware que simplemente utilice su
nombre. La primera etapa ya enlaza módulos originales de SeaBIOS en AArch64,
pero aún no implementa su POST completo, el arranque de sistemas operativos ni UEFI.

**Estado:** v0.9-dev. Las versiones x86 y ARM64 compilan;
las operaciones de disco `drive_s` y `disk_op_s` ya usan política
compartida del SeaBIOS original. QEMU ejecuta cargadores diagnósticos
nativos desde discos legacy MBR/FAT32. No se ha arrancado Linux real
ni Windows ARM64, ni se implementa UEFI.
Consulta [PHASE7_BLOCK_CORE.md](arm64/PHASE7_BLOCK_CORE.md).

---

Upstream SeaBIOS snapshot: `81ec9ec0bcf45df11fb7f98339ec9036b546fca0`.
Original licenses and third-party notices are retained in the repository.

