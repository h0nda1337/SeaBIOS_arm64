# SeaBIOS-ARM64 v1.5-dev: shared block/boot core and Debian ext4

Base: `experimental/arm64-fat32-linux-v1.4`, commit
`2ba717c55fdddbc7765767f981e22fdf202acc0e`. Development branch:
`experimental/arm64-core-v1.5`. Native AArch64, QEMU virt, no UEFI.

## Architecture changes

`src/block.c` itself now compiles and executes on AArch64. Both architectures
use its original `IDMap`, `getDrive`, `getDriveId`, `add_drive` and
`process_op` control flow. ARM64 adapts flat-pointer access, hard-drive count,
geometry mapping, device setup and controller dispatch; x86 retains its
BDA/FDPT, segmented access, EDD and 16/32-bit drivers. Invalid registry types
and null drive lookups are rejected on both architectures.

POST calls the shared `block_setup()` interface after MMIO enumeration.
Registration of every usable `drive_s` occurs before probing or boot-entry
discovery. `arm64/block.c` now provides the hardware backend and convenience
read wrappers, rather than owning a second `process_op` implementation.
Requests up to the original 64 KiB boundary are split into 16-sector controller
segments. On failure `disk_op_s.count` reports exactly the completed segments;
other drives retain their identity and remain usable. Write requests return
write-protected status; firmware remains read-only.

`src/bootlist.c` and `.h` contain the original `bootentry_s` IPL record,
stable insertion and menu reordering extracted from `src/boot.c`. x86 and
ARM64 use the same code. Native drive pointers supply the original driver
type/controller tie-breaks. ARM64 registers an actual `IPL_TYPE_HALT` entry.
Unlisted candidates follow explicit fw_cfg ranks, fixing a bug where a disk
with rank 11 could lose to ARM-only fallback priority 3, 5 or 10. Both the
original x86 boot-menu selection and ARM64 per-disk candidate state remain.

Existing shared `postflow`, `bootprio`, `blockpolicy`, `blockgeom`, `romfile`,
`e820map`, SHA-256 and VirtIO ring sources continue to execute on AArch64.
This revision does not claim that x86 `maininit()`, INT 13h, EDD firmware
calls or all of `src/boot.c` have become native ARM services.

## Storage and handoff robustness

FAT32 validates the entire required file chain before copying executable
bytes and requires EOC on the final cluster. It rejects free/bad/reserved
clusters, cycles, short and overallocated chains, invalid BPB fields and
reserved ExtFlags. Directory cycle detection uses constant memory and avoids
scanning a large volume on a tiny cycle. Failed probes invalidate the previous
volume; failed FAT reads invalidate the cache before DMA can damage it. Active
FAT 1 and per-drive caches are tested. This remains an active-primary-MBR,
8.3-only, read-only reader. It deliberately rejects overallocated files.
Later data I/O failure can leave a partially written destination; no failed
candidate is entered or permanently reserves its load area.

VirtIO initialization confirms reset completion and obtains a consistent
capacity using modern config-generation checks or matching legacy reads.
Completions must identify the submitted head and advance the used ring by
one. Modern reads require the complete data/status length; legacy reads follow
the [VirtIO legacy used-length guidance, section 5.2.6.3](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html)
and clear the bounce buffer first. Controller errors quarantine one drive.
The native handoff resets all discovered queues and confirms status zero;
an unconfirmed reset aborts entry so firmware DMA cannot continue into OS
ownership. This follows the [VirtIO reset contract](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html).

Linux command-line configuration goes through the original SeaBIOS romfile
registry (`etc/linux-cmdline`). The loader checks kernel and initrd placement
together and selects free RAM from the original E820 map when the traditional
address does not fit. FDT preparation preserves valid memreserve entries,
rejects corrupt reservations and limits the final DTB to 2 MiB, following the
[AArch64 Linux boot protocol](https://docs.kernel.org/arch/arm64/booting.html).
Missing optional initrd files are distinguished from corruption or I/O errors.

## Local validation

Toolchain: Debian WSL, Clang/LLD 19.1.7, GCC 14.2, QEMU 10.0.13 TCG.
All targets below were completed locally. Their logs are in `arm64/out/`.

| Check | Result |
| --- | --- |
| ARM64 freestanding build, undefined symbols rejected | PASS |
| Original x86 build, `out/bios.bin` 262144 bytes | PASS |
| `make ARCH=arm64 test` | PASS |
| QEMU diagnostic smoke | 2/2 |
| Independent drive registry | 2/2 |
| Multidisk boot selection | 10/10 |
| FAT32 synthetic Linux/file-initrd | 8/8 |
| Raw MBR/GPT synthetic Linux/initrd/corruption | 16/16 |
| Native legacy FAT32/PE/SHA-256 stages | 34/34 |
| Existing fw_cfg bootorder | 2/2 |
| PE DIR64 relocation/rejection | 4/4 |
| Late fw_cfg rank and strict HALT | 6/6 |
| Adversarial FAT32 volumes | 14/14 |
| Real blkdebug EIO isolation and disk1 fallback | 4/4 |
| Genuine Debian kernel, raw MBR and FAT32 on disk1, v1/v2 | 4/4 |
| Debian ext4 + systemd, disk0/disk1 or 128 MiB, v1/v2 | 6/6 |
| Malformed cmdline, two inputs for each ext4 scenario | 12/12 rejected |

The 102 synthetic QEMU scenarios are separate from the ten genuine Linux
boots. Debian 6.12.111 is an authentic packaged ARM64 kernel. The ext4 matrix
requires installed Debian ARM64 userspace, systemd PID 1, `dpkg`, a writable
disk root and a successful root-file write/read. Its small initramfs only
loads matching VirtIO modules and switches to the disk root. It is still a
test deployment, not a general distribution installer or desktop image.
See [Debian root reproduction, hashes and serial evidence](DEBIAN_EXT4_ROOT.md).

The v1.4 firmware binary was preserved and independently reached its genuine
Linux `/init` marker from FAT32 on disk1 in both MMIO versions before the
v1.5 changes. This establishes the baseline instead of assuming old claims.
GitHub CI results are independent evidence and must be checked in the
`arm64-v15-core.yml` workflow; local results do not imply a green CI run.

Host coverage includes 26 FAT32 tests with UBSan, real shared block-core
registration/64 KiB/bounds/partial-I/O cases, 2400 stable IPL records and
eight bootorder/HALT scenarios, RAM placement/romfile policy and FDT
reservation/size regressions, plus the previous host suite. The initial FAT32
regressions reproduced faults in v1.4, including 138736 reads for a two-cluster
directory cycle; the corrected reader rejects that cycle in fewer than ten.

The full matrix initially exposed an old raw-initrd log assertion and an
overallocated PE test fixture. The raw message was preserved and the fixture
now allocates `ceil(file_size/cluster_size)` clusters; both full suites pass
after these corrections. Failed intermediate runs are not counted as passes.

## Reproduce

```sh
make ARCH=arm64 -j2
make ARCH=arm64 test
make ARCH=arm64 qemu-test qemu-multidrive-test qemu-multiboot-test
make ARCH=arm64 qemu-fat-linux-test qemu-disk-test qemu-legacy-test
make ARCH=arm64 qemu-bootorder-test qemu-reloc-test
make ARCH=arm64 qemu-bootlist-test qemu-fat-corrupt-test qemu-block-failure-test
make -j2
test "$(stat -c%s out/bios.bin)" -eq 262144
# After preparing a genuine Image and the Debian root fixture:
make ARCH=arm64 qemu-real-linux-test ARM64_REAL_LINUX_IMAGE=/path/to/Image
make ARCH=arm64 qemu-root-linux-test ARM64_DEBIAN_ROOT_DISK=/path/to/debian-ext4.img
```

Header dependencies are generated for ARM C objects so header changes trigger
recompilation. `.gitattributes` keeps source/script line endings LF on Windows
checkouts, avoiding WSL shell/Kconfig failures; no unrelated source algorithms
were changed by line-ending normalization.

## Limits and next revision

QEMU virt only; four read-only firmware disks, one outstanding request per
controller, polling and no hotplug. Native IRQ/GIC, PCI/NVMe/USB/network,
ARM interactive boot menu, general allocator/runtime services and physical
hardware/cache-coherency validation remain unfinished. DMA barriers are not
a cache-management implementation for arbitrary real ARM boards. Kernel
memory is capped at 64 MiB and initrd at 16 MiB. FAT LFN/GPT filesystem boot
and distribution-generated initramfs discovery remain unsupported.

Engineering estimate: about **44%** of the full port, within the requested
43–45% direction. The increase from roughly 40% reflects shared original
block/boot-list execution, verified failure isolation, safe RAM selection
and disk-backed Debian userspace. This is a qualitative judgment, not source
coverage, subsystem parity, hardware readiness or Windows compatibility.

For v1.6: preserve these regressions; add PARTUUID/UUID root selection and
test a distribution-generated initramfs; design a genuine shared VirtIO block
backend for PCI and MMIO; then address native IRQ/timers and a boot menu using
the shared IPL records. Validate DMA cache rules and entry-state requirements
before attempting a physical board. See [Windows ARM64 constraints](WINDOWS_ARM64_CONSTRAINTS.md).
