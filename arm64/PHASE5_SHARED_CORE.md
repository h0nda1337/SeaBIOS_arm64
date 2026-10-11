# Phase 5 — Shared SeaBIOS boot priorities and SHA-256 (v0.7-dev)

## Design objective

Continue the genuine SeaBIOS port: transfer **original SeaBIOS functionality** to
AArch64, not just add an unrelated ARM firmware or a misleading Windows-boot claim.
This branch remains strictly **native ARM64 / MBR / FAT32; no UEFI**.

## Shared boot path logic

- `src/bootprio.c` and `.h` hold the original prefix/wildcard-matching logic
  extracted from `src/boot.c`. The x86 core now calls that shared implementation.
  The ARM64 core also compiles the same source instead of a cloned algorithm.
- A bounded `bootprio_find_text()` adapts the logic for QEMU's `fw_cfg`
  `bootorder` blob (newline-separated paths, often **NUL-terminated**). The
  file is limited to 4 KiB, with a maximum 255-byte entry and strict embedded
  NUL rejection.
- `arm64/bootorder.c` consumes the original `romfile` registry and assigns
  firmware-provided priorities to the discovered VirtIO-MMIO block device.
  It recognizes QEMU paths of the form `/virtio-mmio@xxxxxxxxxxxxxxxx/*`.
  HALT, when present, suppresses fallback entries after its position.
- ARM64 hardware/device initialization remains separate. This is a port of
  the **boot-order matching algorithm**, not `src/boot.c` in its entirety.

## Shared SHA-256 algorithm and MBR stage-2 checks

- `src/sha256.c`, the original IBM-contributed SeaBIOS SHA-256 implementation,
  now has minimal AArch64 rotate/byte-order helper functions. x86's existing
  assembly-backed operations are retained in the x86 branch.
- Legacy `SBARMLE1` FNV32 containers still load as before.
- New `SBARMLE2` containers carry an SHA-256 digest of the raw stage code in
  bytes 16..47 of their 64-byte header. The firmware hashes the code using
  **the original SeaBIOS implementation** and verifies it before handoff.
- **Integrity is not authentication.** The hash can be replaced by anyone who
  can alter the disk; it is not secure boot, signature verification or proof of
  developer origin. Stage-2 executables remain synthetic experimental native
  AArch64 code, not Microsoft binaries.

## Verified behavior and tests

- Cross-compilation of the AArch64 firmware with upstream source objects
  and clean compilation of the 256 KiB x86 `bios.bin`.
- QEMU 10.0.13 smoke boot without disk and synthetic diagnostic handoff.
- QEMU 10.0.13 with `bootindex=1` and `fw_cfg` `bootorder`: confirmed a real
  firmware path match and successful FAT32 PE32+ handoff. v1/v2 MMIO verified.
- Unit tests of original SHA-256 output against Python `hashlib` on 11 lengths,
  including SHA padding boundaries, plus 12 bootorder parser tests.
- Legacy FAT32 test matrix including two SHA-256 good containers and two
  corrupt-digest reject cases (both VirtIO-MMIO v1 and v2).

Run:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-bootorder-test
make ARCH=arm64 qemu-legacy-test
```

The `qemu-legacy-test` suite may take over two minutes under emulation.

## Not implemented

No genuine Windows ARM64 chainload; no Windows loader, NT kernel, HAL,
OS driver stack, memory handoff or ACPI platform contract. No Linux kernel
boot proved. The full original SeaBIOS POST and original IPL boot engine
are not ported. SHA-256 hashing is not signature verification. No firmware
flashing or physical ARM SoC validation has been done.
