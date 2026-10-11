# SeaBIOS AArch64 phase 6 — shared IPL sorting and native PE relocation (v0.8-dev)

**Experimental, QEMU-only, legacy MBR/FAT32. No UEFI code. No Windows ARM64 boot.**

## Source continuity: upstream IPL ordering

The original SeaBIOS `src/boot.c::bootentry_add()` inserts candidates into
`BootList` ordered by priority, legacy IPL type, and (for disk classes) drive
type and controller identity. Its comparator is now factored into
`src/bootprio.c::bootprio_precedes()`, and both x86 `bootentry_add()` and the
AArch64 `arm64_boot_register()` call this same source. The existing SeaBIOS
linked-list data structure and x86-specific disk mechanics remain intact.

`src/bootprio.c` continues to implement the same bootorder path glob matching
shared in v0.7. Shared code is not a complete port of the original x86 IPL
engine: it does not expose x86 INT 19h, BCV/BEV, VGA boot menu or real-mode
interrupts on AArch64. Matching/priority and sorted candidate selection are
now shared; boot execution remains architecture-specific.

## Strictly non-EFI PE32+ ARM64 staging extension

Native MBR/FAT32 stage-2 loading supports import-free ARM64 PE32+ executables
linked at an address DIFFERENT from their mapped RAM address. It consumes the
PE base-relocation directory (data-directory index 5), parses block headers,
checks each entry, and applies only `IMAGE_REL_BASED_DIR64` relocations.

- Allowed: `IMAGE_REL_BASED_ABSOLUTE` padding and `DIR64` pointers into the
  image; copied into an explicitly bounded 4 MiB RAM window.
- Rejected: unsupported relocation types; malformed, out-of-range or
  overlapping sections; a required but missing relocation directory; imports,
  delay imports, TLS callbacks, CLR and EFI subsystem images.
- For safety and simplicity, relocations of pointers OUTSIDE the image are
  rejected. This is not a general-purpose Windows PE loader.
- The synthetic staging test links at `0x4b000000` and maps at `0x4a000000`.
  A deliberately absolute pointer in `.rdata` must be adjusted before the
  program can print its QEMU serial success message.

**A PE executable format is NOT the Windows boot protocol.** There is still
no Windows `bootmgr`, BCD loader, `winload`, kernel initialization, HAL or NT
handoff and no firmware implementation of UEFI, by project policy. No claim
of booting Microsoft software is made.

## Validation

```sh
make ARCH=arm64
make ARCH=arm64 test
make ARCH=arm64 qemu-test
make ARCH=arm64 qemu-disk-test
make ARCH=arm64 qemu-bootorder-test
make ARCH=arm64 qemu-legacy-test
make ARCH=arm64 qemu-reloc-test
make                  # Original SeaBIOS x86 build
```

`qemu-reloc-test` contains v1 and v2 VirtIO-MMIO smoke tests for the relocated
PE stage, plus negative tests with a deliberately corrupted base-relocation
entry. `test_shared_core.py` adds 12 IPL comparator cases and retains SHA-256
and bootorder tests. All QEMU images are synthetic, not Windows or Linux.

This architecture remains transitional. There is no full POST on AArch64,
BIOS interrupt ABI, physical device support, or real operating-system boot.
