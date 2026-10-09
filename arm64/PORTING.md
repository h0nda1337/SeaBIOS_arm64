# AArch64 integration notes — v0.2-dev (NOT TESTED)

## What is directly shared with upstream SeaBIOS

- `src/e820map.c` is compiled as an AArch64 object. Its original
  insertion/merge/overlap algorithm is used by `arm64/post.c` to describe RAM
  and reserved regions discovered through FDT. This is an **internal map**, not
  the PC `INT 15h E820` interface.
- `src/list.h` is used directly by `arm64/bootorder.c` for boot candidates.
- Original x86 sources and default `make` remain available. `make ARCH=arm64`
  routes to the ARM build; `make -f arm64/Makefile` remains supported.

## Temporary AArch64 substitutes

- `arm64/string.c` provides freestanding `memmove`, since upstream
  `src/string.c` includes x86 instructions and segmented-address helpers.
- `arm64/compat.c` suppresses legacy debug printf formatting; full SeaBIOS
  logging is not ported yet. The ARM POST scaffold reports the memory map
  through PL011 directly.
- `arm64/post.c` mirrors the separation of interface and boot setup but
  **does not compile or execute `src/post.c`**; the latter depends on IVT,
  BDA/EBDA and x86 INT19. This is intentional, not a completed port.
- `arm64/bootorder.c` uses SeaBIOS linked-list primitives, but it is not
  upstream `src/boot.c` and does not boot an operating system.

## Integration strategy (not yet implemented)

1. Rework upstream x86-specific types/memory access behind real architecture
   interfaces; keep the 16/32-bit x86 build intact.
2. Port genuine SeaBIOS core subsystems piece by piece, with meaningful
   shared code and no misleading function-name-only shims.
3. Add ARM64 exception handling, timer, MMIO/PCI discovery and virtio block.
4. Implement a real ARM64 kernel boot ABI and eventually a compatible UEFI
   environment for Windows on ARM (long-term research, not a feature).

## Warning

**No compile, host-test or QEMU execution was performed for v0.2-dev.**
All modifications are provisional and must be reviewed and tested before
being treated as working firmware. Do not flash onto real devices.
