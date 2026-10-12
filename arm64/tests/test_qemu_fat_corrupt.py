#!/usr/bin/env python3
"""Boot/reject adversarial FAT32 volumes through real VirtIO-MMIO v1/v2."""
import importlib.util
from pathlib import Path
import struct
import tempfile

from test_qemu_fat_linux import ARM, SUCCESS, run

spec = importlib.util.spec_from_file_location(
    'fat_fixture', ARM / 'tools/create_fat32_linux_disk.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


def mutate(disk, kind, image_bytes):
    bps = fixture.SECTOR
    fat_lba = fixture.START + fixture.RESERVED
    data_lba = fat_lba + fixture.FAT_SECTORS
    with disk.open('r+b') as stream:
        def write(lba, data):
            stream.seek(lba * bps)
            stream.write(data)

        def fat(cluster, value):
            stream.seek(fat_lba * bps + cluster * 4)
            stream.write(struct.pack('<I', value))

        if kind == 'directory-cycle':
            fat(fixture.ROOT, 129)
            fat(129, fixture.ROOT)
            write(data_lba + fixture.ROOT - 2, b'\xe5' * bps)
            write(data_lba + 129 - 2, b'\xe5' * bps)
            return

        if kind == 'reserved-flags':
            stream.seek(fixture.START * bps + 40)
            stream.write(struct.pack('<H', 0x8000))
            return

        chain = []
        cluster = 4
        for _ in range((image_bytes + bps - 1) // bps):
            chain.append(cluster)
            stream.seek(fat_lba * bps + cluster * 4)
            cluster = struct.unpack('<I', stream.read(4))[0] & 0x0fffffff

        if kind == 'active-second-fat':
            # Add another FAT and move the data area accordingly. Only copy
            # populated fixture sectors; the remainder stays sparse.
            stream.seek(fat_lba * bps)
            table = stream.read(fixture.FAT_SECTORS * bps)
            stream.seek(data_lba * bps)
            contents = stream.read((max(chain + [fixture.ROOT, fixture.BOOT]) - 1) * bps)
            write(data_lba + fixture.FAT_SECTORS, contents)
            write(fat_lba + fixture.FAT_SECTORS, table)
            write(fat_lba, b'\0' * len(table))
            stream.seek(fixture.START * bps + 16)
            stream.write(b'\x02')
            stream.seek(fixture.START * bps + 40)
            stream.write(struct.pack('<H', 0x81))
        elif kind == 'final-free':
            fat(chain[-1], 0)
        elif kind == 'final-bad':
            fat(chain[-1], 0x0ffffff7)
        elif kind == 'file-cycle':
            fat(chain[-1], chain[0])
        elif kind == 'extra-cluster':
            extra = max(chain) + 50
            fat(chain[-1], extra)
            fat(extra, 0x0fffffff)
        else:
            raise ValueError(kind)


def main():
    image = ARM / 'out/linux-image-diagnostic.bin'
    image_bytes = image.stat().st_size
    cases = ('active-second-fat', 'final-free', 'final-bad', 'file-cycle',
             'extra-cluster', 'directory-cycle', 'reserved-flags')
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        disks = {}
        for case in cases:
            disk = directory / (case + '.img')
            fixture.make_disk(image, disk)
            mutate(disk, case, image_bytes)
            disks[case] = disk
        for modern in (False, True):
            for case, disk in disks.items():
                output = run(disk, modern=modern)
                if case == 'active-second-fat':
                    if SUCCESS not in output:
                        raise AssertionError('active second FAT boot failed:\n' + output)
                else:
                    if SUCCESS in output:
                        raise AssertionError('corrupt FAT booted (' + case + '):\n' + output)
                    required = '[boot] not found'
                    if case not in ('directory-cycle', 'reserved-flags'):
                        required = '[linux] FAT32 kernel chain invalid or truncated'
                    if required not in output:
                        raise AssertionError('missing rejection (' + case + '):\n' + output)
                print('PASS FAT32', case, 'v2' if modern else 'v1', flush=True)


if __name__ == '__main__':
    main()
