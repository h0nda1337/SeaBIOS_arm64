# SeaBIOS-ARM64 — Phase 7: shared block operations (v0.9-dev)

**Experimental AArch64, QEMU `virt` only. No UEFI, no Windows ARM64 boot.**

## Why this changes the architecture, not just the bootloader

Earlier ARM64 boot paths called the VirtIO-MMIO backend directly. The original
SeaBIOS code uses `struct drive_s` and `struct disk_op_s` from `src/block.h`,
with command handlers and common transfer rules. Phase 7 introduces an ARM64
block dispatcher (`arm64/block.c`) that uses those **original** data structures
and the same `process_op()` command ABI, without implementing x86 INT 13h.

All existing ARM64 consumers keep the same public `arm64_virtio_blk_read()` and
`arm64_virtio_blk_read_many()` calls, but they now enter the structured
`disk_op_s` layer. Only the private backend calls
`arm64_virtio_blk_read_many_transport()`. One read-only QEMU VirtIO-MMIO drive
is currently supported; a generic multi-controller registry and x86 legacy
interrupt services have *not* been ported.

## Code shared with x86 SeaBIOS

`src/blockpolicy.c` and `src/blockpolicy.h` factor **three pieces of original
SeaBIOS `src/block.c` behavior** into a truly shared implementation:

- The 64-KiB maximum transfer boundary, including zeroing the block count when
  exceeded (`blockop_prepare`).
- Error handling preserving partial completion, but zeroing a full untouched
  count on an error (`blockop_finish`).
- The original default success/rejection policy for disk commands whose
  controller has no handler (`default_process_op`).

Both the x86 `process_op()` and ARM64 implementation use the first two helpers.
The x86 handlers and disk geometry/BIOS mapping are still in `src/block.c`.
The block-size argument is read using `GET_FLATPTR` in x86 before calling the
shared helper; this preserves segmented pointer semantics. The ARM64 backend
uses standard pointers instead. `default_process_op` is compiled once per
architecture from the same shared source.

## Validation

Build and test in the repository root:

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-bootorder-test
make
```

Tested with QEMU 10.0.13 on an x86_64 Linux host:

- x86 SeaBIOS build produced `out/bios.bin` (262,144 bytes).
- ARM64 firmware compiled/linked, and the diagnostic payload booted.
- 262/262 host-side command and transfer-policy cases passed.
- QEMU legacy/modern VirtIO fw_cfg priority checks passed (2/2).
- Six selected real QEMU MBR/FAT32 cases passed across VirtIO v1/v2: valid
  fragmented native stage, corrupt FAT chain, and PE32+ native stage.
- The **full** synthetic Linux disk matrix exceeded the environment's time
  limit; this is explicitly **not** counted as passing.

A successful ARM64 diagnostic or import-free PE stage2 is not equivalent to a
Windows NT loader, nor has a real Linux kernel been boot-tested by this change.

## Next engineering obstacles

- Multi-drive registration and controller lifetime; resilient retries and
  isolation of transport failures.
- Reuse more original SeaBIOS POST and boot device registration code, while
  retaining functional x86 BIOS builds.
- Real OS handoff testing (Linux ARM64 first), then research native NT
  initialization for a legacy ARM64 boot environment **without UEFI**.
