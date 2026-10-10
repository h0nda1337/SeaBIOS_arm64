# Testing evidence and scope

- `test_fdt.py`: 10 host tests of the Device Tree parser.
- `test_payload.py`: 4 host tests of the legacy diagnostic payload format.
- `test_linuximage.py`: 10 host tests of ARM64 Linux Image header validation.
- `test_gptcrc.py`: 5 host tests of CRC-32, comparing to Python zlib.
- `test_qemu.py`: two QEMU smoke runs (empty firmware and legacy diagnostic).
- `test_qemu_disk.py`: ten QEMU runs (VirtIO v1 and v2, each with MBR/GPT
  valid MBR/GPT images, invalid GPT CRCs and invalid Image headers).

All QEMU runs are synthetic firmware tests, not boots of Linux or Windows.
The produced disk images are created under `arm64/out/` and are never written
to user drives.
