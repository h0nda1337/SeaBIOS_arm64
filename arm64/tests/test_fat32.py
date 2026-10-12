#!/usr/bin/env python3
"""Run the firmware FAT32 reader against sparse, adversarial host disks."""
import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ARM = Path(__file__).resolve().parents[1]
SECTOR = 512
IMAGE = b'IMAGE      '
BOOT = b'BOOT       '


class Volume(ctypes.Structure):
    _fields_ = [('part_lba', ctypes.c_uint64), ('fat_lba', ctypes.c_uint64),
                ('data_lba', ctypes.c_uint64), ('fat_sectors', ctypes.c_uint32),
                ('root_cluster', ctypes.c_uint32), ('clusters', ctypes.c_uint32),
                ('spc', ctypes.c_uint8)]


class Context(ctypes.Structure):
    _fields_ = [('volume', Volume), ('disk_index', ctypes.c_uint32),
                ('cache_lba', ctypes.c_uint64),
                ('fat_sector', ctypes.c_uint8 * SECTOR),
                ('sector', ctypes.c_uint8 * SECTOR)]


MOCK = r'''
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "include/fat32.h"
#define MAP_COUNT 1024
#define DISKS 3
struct mapped_sector {
    uint32_t disk;
    uint64_t lba;
    uint8_t bytes[512];
};
static struct mapped_sector map[MAP_COUNT];
static unsigned used, reads, invalid_reads;
static uint64_t capacities[DISKS], fail_lba[DISKS];
void mock_reset(void) {
    used = reads = invalid_reads = 0;
    memset(capacities, 0, sizeof(capacities));
    for (unsigned d = 0; d < DISKS; d++) fail_lba[d] = UINT64_MAX;
}
void mock_capacity(uint32_t disk, uint64_t sectors) {
    if (disk >= DISKS) abort();
    capacities[disk] = sectors;
}
void mock_failure(uint32_t disk, uint64_t lba) {
    if (disk >= DISKS) abort();
    fail_lba[disk] = lba;
}
void mock_sector(uint32_t disk, uint64_t lba, const void *bytes) {
    unsigned n;
    for (n = 0; n < used; n++)
        if (map[n].disk == disk && map[n].lba == lba) break;
    if (n == used) {
        if (used == MAP_COUNT) abort();
        used++;
    }
    map[n].disk = disk;
    map[n].lba = lba;
    memcpy(map[n].bytes, bytes, 512);
}
unsigned mock_reads(void) { return reads; }
unsigned mock_invalid_reads(void) { return invalid_reads; }
unsigned mock_context_bytes(void) { return sizeof(struct fat32_context); }
uint64_t arm64_virtio_blk_capacity_at(uint32_t disk) {
    return disk < DISKS ? capacities[disk] : 0;
}
int arm64_virtio_blk_read_at(uint32_t disk, uint64_t lba, void *out) {
    reads++;
    if (disk >= DISKS || lba >= capacities[disk]) {
        invalid_reads++;
        return -1;
    }
    if (lba == fail_lba[disk]) {
        memset(out, 0xa5, 512);  /* Failed DMA can still change the buffer. */
        return -1;
    }
    for (unsigned n = 0; n < used; n++)
        if (map[n].disk == disk && map[n].lba == lba) {
            memcpy(out, map[n].bytes, 512);
            return 0;
        }
    memset(out, 0, 512);
    return 0;
}
'''


def entry(name, cluster, size=0, attrs=0x20):
    result = bytearray(32)
    result[:11] = name
    result[11] = attrs
    struct.pack_into('<H', result, 20, cluster >> 16)
    struct.pack_into('<H', result, 26, cluster & 0xffff)
    struct.pack_into('<I', result, 28, size)
    return result


class Disk:
    def __init__(self, lib, disk=0, start=2048, total=70000, spc=1,
                 fats=1, fat_sectors=600, flags=0):
        self.lib, self.index = lib, disk
        self.start, self.total, self.spc = start, total, spc
        self.fats, self.fat_sectors = fats, fat_sectors
        self.fat_lba = start + 32
        self.data_lba = self.fat_lba + fats * fat_sectors
        self.clusters = (total - 32 - fats * fat_sectors) // spc
        self.bpb = bytearray(SECTOR)
        struct.pack_into('<H', self.bpb, 11, SECTOR)
        self.bpb[13], self.bpb[16] = spc, fats
        struct.pack_into('<H', self.bpb, 14, 32)
        struct.pack_into('<I', self.bpb, 32, total)
        struct.pack_into('<I', self.bpb, 36, fat_sectors)
        struct.pack_into('<H', self.bpb, 40, flags)
        struct.pack_into('<I', self.bpb, 44, 2)
        self.bpb[510:] = b'\x55\xaa'
        self.mbr = bytearray(SECTOR)
        self.mbr[446], self.mbr[450] = 0x80, 0x0c
        struct.pack_into('<II', self.mbr, 454, start, total)
        self.mbr[510:] = b'\x55\xaa'
        self.sectors = {}
        lib.mock_capacity(disk, start + total)
        self.write(0, self.mbr)
        self.write(start, self.bpb)
        for f in range(fats):
            self.fat(0, 0x0ffffff8, f)
            self.fat(1, 0x0fffffff, f)
            self.fat(2, 0x0fffffff, f)

    def write(self, lba, contents):
        value = bytes(contents).ljust(SECTOR, b'\0')
        assert len(value) == SECTOR
        self.sectors[lba] = value
        self.lib.mock_sector(self.index, lba, ctypes.create_string_buffer(value))

    def update_bpb(self):
        self.write(self.start, self.bpb)

    def fat(self, cluster, successor, copy=0):
        lba = self.fat_lba + copy * self.fat_sectors + cluster * 4 // SECTOR
        value = bytearray(self.sectors.get(lba, b'\0' * SECTOR))
        struct.pack_into('<I', value, cluster * 4 % SECTOR, successor)
        self.write(lba, value)

    def data(self, cluster, contents, sector=0):
        self.write(self.data_lba + (cluster - 2) * self.spc + sector, contents)

    def probe(self):
        context = Context()
        result = self.lib.arm64_fat32_probe_active(ctypes.byref(context), self.index)
        return result, context


class Fat32Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        (directory / 'mock.c').write_text(MOCK)
        library = directory / 'fat32.so'
        source = os.getenv('FAT32_SOURCE', str(ARM / 'fat32.c'))
        subprocess.run([os.getenv('CC', 'cc'), '-std=c11', '-O2', '-shared',
                        '-fPIC', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I' + str(ARM), source, str(directory / 'mock.c'),
                        '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.mock_capacity.argtypes = [ctypes.c_uint32, ctypes.c_uint64]
        cls.lib.mock_failure.argtypes = [ctypes.c_uint32, ctypes.c_uint64]
        cls.lib.mock_sector.argtypes = [ctypes.c_uint32, ctypes.c_uint64,
                                       ctypes.c_void_p]
        cls.lib.arm64_fat32_probe_active.argtypes = [ctypes.POINTER(Context),
                                                   ctypes.c_uint32]
        cls.lib.arm64_fat32_find.argtypes = [ctypes.POINTER(Context),
                ctypes.c_uint32, ctypes.c_char_p, ctypes.c_int,
                ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
        cls.lib.arm64_fat32_load.argtypes = [ctypes.POINTER(Context),
                ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
        cls.lib.arm64_fat32_first_sector.argtypes = [ctypes.POINTER(Context),
                ctypes.c_uint32, ctypes.c_void_p]
        assert cls.lib.mock_context_bytes() == ctypes.sizeof(Context)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.mock_reset()

    def tearDown(self):
        self.assertEqual(self.lib.mock_invalid_reads(), 0,
                         'reader attempted an out-of-device access')

    def find(self, context, directory=2, name=IMAGE, isdir=0):
        cluster, size = ctypes.c_uint32(111), ctypes.c_uint32(222)
        rc = self.lib.arm64_fat32_find(ctypes.byref(context), directory,
                                     name, isdir, ctypes.byref(cluster),
                                     ctypes.byref(size))
        return rc, cluster.value, size.value

    def load(self, context, cluster, size, capacity=None):
        capacity = size if capacity is None else capacity
        output = ctypes.create_string_buffer(b'\xa5' * (capacity + 16), capacity + 16)
        rc = self.lib.arm64_fat32_load(ctypes.byref(context), cluster, size,
                                     output, capacity)
        self.assertEqual(output.raw[capacity:], b'\xa5' * 16,
                         'load wrote beyond its destination capacity')
        return rc, output.raw[:capacity]

    def test_fragmented_file_and_exact_final_byte(self):
        disk = Disk(self.lib)
        disk.fat(4, 129)
        disk.fat(129, 8)
        disk.fat(8, 0xaffffff8)  # Reserved upper nibble is ignored.
        disk.data(4, b'A' * SECTOR)
        disk.data(129, b'B' * SECTOR)
        disk.data(8, b'C' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        rc, output = self.load(context, 4, 2 * SECTOR + 7)
        self.assertEqual(rc, 0)
        self.assertEqual(output, b'A' * SECTOR + b'B' * SECTOR + b'C' * 7)

    def test_active_second_fat_is_used(self):
        disk = Disk(self.lib, fats=2, flags=0x81)
        disk.fat(4, 0, copy=0)
        disk.fat(4, 129, copy=1)
        disk.fat(129, 0x0fffffff, copy=1)
        disk.data(4, b'A' * SECTOR)
        disk.data(129, b'B' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(context.volume.fat_lba, disk.fat_lba + disk.fat_sectors)
        self.assertEqual(self.load(context, 4, 513), (0, b'A' * 512 + b'B'))

    def test_mirrored_fat_ignores_active_index(self):
        disk = Disk(self.lib, fats=2, flags=0x0f)
        disk.fat(4, 0x0fffffff, copy=0)
        disk.fat(4, 0, copy=1)
        disk.data(4, b'M' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.load(context, 4, 1), (0, b'M'))

    def test_multi_sector_cluster(self):
        disk = Disk(self.lib, total=200000, spc=2, fat_sectors=800)
        disk.fat(4, 7)
        disk.fat(7, 0x0fffffff)
        disk.data(4, b'A' * SECTOR)
        disk.data(4, b'B' * SECTOR, sector=1)
        disk.data(7, b'C' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.load(context, 4, 1031),
                         (0, b'A' * 512 + b'B' * 512 + b'C' * 7))

    def test_maximum_supported_cluster_size(self):
        disk = Disk(self.lib, total=4500000, spc=64, fat_sectors=600)
        disk.fat(4, 0x0fffffff)
        disk.data(4, b'A' * SECTOR)
        disk.data(4, b'Z' * SECTOR, sector=63)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        rc, output = self.load(context, 4, 64 * SECTOR)
        self.assertEqual(rc, 0)
        self.assertEqual(output[:SECTOR], b'A' * SECTOR)
        self.assertEqual(output[-SECTOR:], b'Z' * SECTOR)

    def test_single_cluster_status_is_checked_before_copy(self):
        for status in (0, 1, 0x0ffffff0, 0x0ffffff6, 0x0ffffff7, 70000):
            with self.subTest(status=hex(status)):
                disk = Disk(self.lib)
                disk.fat(4, status)
                disk.data(4, b'unsafe' * 80)
                rc, context = disk.probe()
                self.assertEqual(rc, 0)
                rc, output = self.load(context, 4, 7)
                self.assertNotEqual(rc, 0)
                self.assertEqual(output, b'\xa5' * 7)

    def test_short_chain_does_not_write_destination(self):
        disk = Disk(self.lib)
        disk.fat(4, 0x0fffffff)
        disk.data(4, b'A' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        rc, output = self.load(context, 4, 513)
        self.assertNotEqual(rc, 0)
        self.assertEqual(output, b'\xa5' * 513)

    def test_overallocated_chain_is_rejected(self):
        disk = Disk(self.lib)
        disk.fat(4, 5)
        disk.fat(5, 0x0fffffff)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertNotEqual(self.load(context, 4, 512)[0], 0)

    def test_self_cycle_is_rejected_even_for_one_byte(self):
        disk = Disk(self.lib)
        disk.fat(4, 4)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        rc, output = self.load(context, 4, 1)
        self.assertNotEqual(rc, 0)
        self.assertEqual(output, b'\xa5')

    def test_two_cluster_cycle_rejects_promptly(self):
        disk = Disk(self.lib)
        disk.fat(4, 129)
        disk.fat(129, 4)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        reads = self.lib.mock_reads()
        rc, output = self.load(context, 4, 2 * 1024 * 1024)
        self.assertNotEqual(rc, 0)
        self.assertEqual(output[:512], b'\xa5' * 512)
        self.assertLess(self.lib.mock_reads() - reads, 10)

    def test_directory_cycle_rejects_promptly(self):
        disk = Disk(self.lib)
        disk.fat(2, 129)
        disk.fat(129, 2)
        # A directory without an end marker must traverse its FAT chain.
        disk.data(2, b'\xe5' * SECTOR)
        disk.data(129, b'\xe5' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        reads = self.lib.mock_reads()
        self.assertEqual(self.find(context), (-1, 111, 222))
        self.assertLess(self.lib.mock_reads() - reads, 10)

    def test_long_acyclic_directory_is_not_mistaken_for_cycle(self):
        disk = Disk(self.lib)
        chain = list(range(2, 260))
        for current, successor in zip(chain, chain[1:] + [0x0fffffff]):
            disk.fat(current, successor)
            disk.data(current, b'\xe5' * SECTOR)
        disk.data(chain[-1], entry(IMAGE, 400, 1))
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.find(context), (0, 400, 1))

    def test_directory_skips_deleted_lfn_and_volume_entries(self):
        disk = Disk(self.lib)
        deleted = entry(IMAGE, 4, 9)
        deleted[0] = 0xe5
        disk.data(2, deleted + entry(IMAGE, 4, 9, 0x0f) +
                  entry(IMAGE, 4, 9, 0x08) + entry(IMAGE, 5, 12))
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.find(context), (0, 5, 12))

    def test_directory_end_marker_and_eoc_return_not_found(self):
        disk = Disk(self.lib)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.find(context), (-2, 111, 222))
        disk.data(2, b'\xe5' * SECTOR)
        self.assertEqual(self.find(context), (-2, 111, 222))

    def test_empty_regular_file_may_have_cluster_zero(self):
        disk = Disk(self.lib)
        disk.data(2, entry(IMAGE, 0, 0))
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.find(context), (0, 0, 0))

    def test_invalid_directory_entry_leaves_outputs_unchanged(self):
        disk = Disk(self.lib)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        for cluster in (0, 1, disk.clusters + 2, 0x0ffffff0, 0xf0000004):
            with self.subTest(cluster=hex(cluster)):
                disk.data(2, entry(IMAGE, cluster, 1))
                self.assertEqual(self.find(context), (-1, 111, 222))
        disk.data(2, entry(BOOT, 4, 1, 0x10))
        self.assertEqual(self.find(context, name=BOOT, isdir=1), (-1, 111, 222))

    def test_capacity_rejection_does_not_copy(self):
        disk = Disk(self.lib)
        disk.fat(4, 0x0fffffff)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        reads = self.lib.mock_reads()
        self.assertEqual(self.load(context, 4, 513, capacity=512),
                         (-1, b'\xa5' * 512))
        self.assertEqual(self.lib.mock_reads(), reads)

    def test_last_usable_cluster_is_in_bounds(self):
        disk = Disk(self.lib)
        last = disk.clusters + 1
        disk.fat(last, 0x0fffffff)
        disk.data(last, b'L' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.load(context, last, 1), (0, b'L'))
        self.assertNotEqual(self.load(context, last + 1, 1)[0], 0)

    def test_io_failure_is_propagated(self):
        disk = Disk(self.lib)
        disk.fat(4, 0x0fffffff)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.lib.mock_failure(0, disk.fat_lba)
        self.assertNotEqual(self.load(context, 4, 1)[0], 0)
        self.lib.mock_failure(0, disk.data_lba)
        self.assertEqual(self.find(context), (-1, 111, 222))
        self.lib.mock_failure(0, disk.data_lba + 2)
        self.assertEqual(self.load(context, 4, 1)[0], -3)

    def test_reprobe_another_drive_invalidates_fat_cache(self):
        first = Disk(self.lib, disk=0)
        second = Disk(self.lib, disk=1)
        first.fat(4, 0x0fffffff)
        second.fat(4, 0)
        first.data(4, b'A' * SECTOR)
        rc, context = first.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.load(context, 4, 1), (0, b'A'))
        self.assertEqual(self.lib.arm64_fat32_probe_active(ctypes.byref(context), 1), 0)
        self.assertNotEqual(self.load(context, 4, 1)[0], 0)

    def test_failed_fat_read_cannot_poison_previous_cache_entry(self):
        disk = Disk(self.lib)
        disk.fat(4, 5)
        disk.fat(5, 0x0fffffff)
        disk.fat(129, 130)
        disk.fat(130, 0x0fffffff)
        disk.data(4, b'A' * SECTOR)
        disk.data(5, b'B' * SECTOR)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(self.load(context, 4, 513), (0, b'A' * SECTOR + b'B'))
        self.lib.mock_failure(0, disk.fat_lba + 1)
        self.assertNotEqual(self.load(context, 129, 513)[0], 0)
        self.lib.mock_failure(0, 0xffffffffffffffff)
        self.assertEqual(self.load(context, 4, 513), (0, b'A' * SECTOR + b'B'))

    def test_failed_probe_invalidates_prior_volume(self):
        disk = Disk(self.lib)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        disk.bpb[510] = 0
        disk.update_bpb()
        self.assertNotEqual(self.lib.arm64_fat32_probe_active(ctypes.byref(context), 0), 0)
        self.assertEqual(context.volume.clusters, 0)
        self.assertEqual(context.volume.spc, 0)
        self.assertNotEqual(self.lib.arm64_fat32_first_sector(ctypes.byref(context), 2,
                            ctypes.create_string_buffer(SECTOR)), 0)

    def test_bpb_rejects_non_fat32_and_reserved_fields(self):
        for offset, fmt, value in ((11, '<H', 4096), (13, '<B', 3),
                (13, '<B', 128), (14, '<H', 0), (16, '<B', 0),
                (17, '<H', 1), (19, '<H', 1), (22, '<H', 1),
                (36, '<I', 0), (40, '<H', 0x8000), (40, '<H', 0x82),
                (42, '<H', 1), (44, '<I', 1), (44, '<I', 70000)):
            with self.subTest(offset=offset, value=value):
                disk = Disk(self.lib)
                struct.pack_into(fmt, disk.bpb, offset, value)
                disk.update_bpb()
                self.assertNotEqual(disk.probe()[0], 0)

    def test_fat_capacity_and_volume_size_bounds(self):
        for options in ({'total': 65000}, {'fat_sectors': 1},
                        {'fats': 2, 'fat_sectors': 40000}):
            with self.subTest(options=options):
                self.assertNotEqual(Disk(self.lib, **options).probe()[0], 0)
        disk = Disk(self.lib)
        struct.pack_into('<I', disk.bpb, 32, disk.total + 1)
        disk.update_bpb()
        self.assertNotEqual(disk.probe()[0], 0)

    def test_reserved_cluster_numbers_cannot_be_allocated(self):
        max_clusters = 0x0fffffee
        fat_sectors = ((max_clusters + 2) * 4 + SECTOR - 1) // SECTOR
        total = 32 + fat_sectors + max_clusters
        disk = Disk(self.lib, total=total, fat_sectors=fat_sectors)
        rc, context = disk.probe()
        self.assertEqual(rc, 0)
        self.assertEqual(context.volume.clusters, max_clusters)
        self.lib.mock_capacity(0, disk.start + total + 1)
        struct.pack_into('<I', disk.mbr, 458, total + 1)
        disk.write(0, disk.mbr)
        struct.pack_into('<I', disk.bpb, 32, total + 1)
        disk.update_bpb()
        self.assertNotEqual(disk.probe()[0], 0)

    def test_mbr_partition_bounds_are_checked_before_read(self):
        disk = Disk(self.lib)
        for start, count in ((0, disk.total), (2048, 0),
                             (disk.start + disk.total, 1),
                             (0xffffffff, 0xffffffff)):
            with self.subTest(start=start, count=count):
                struct.pack_into('<II', disk.mbr, 454, start, count)
                disk.write(0, disk.mbr)
                self.assertNotEqual(disk.probe()[0], 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
