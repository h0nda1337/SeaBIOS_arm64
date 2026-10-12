#!/usr/bin/env python3
"""Regress original IPL sorting and the real ARM64 bootorder adapter on a host.

The independent oracle uses Python's stable tuple sort. The adapter tests
execute arm64/bootorder.c with fw_cfg and drive_s fixtures, in fresh processes.
"""
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

LIST_FIXTURE = r'''
#include "block.h"
#include "bootlist.h"

struct record {
    struct bootentry_s entry;
    unsigned id;
};
static struct hlist_head list;
static struct record records[256];
static struct drive_s drives[256];

void reset(void) { list.first = 0; }
void add(unsigned id, int type, int priority, unsigned driver, unsigned cntl)
{
    struct record *r = &records[id];
    r->id = id;
    drives[id].type = driver;
    drives[id].cntl_id = cntl;
    r->entry.drive = &drives[id];
    bootlist_add(&list, &r->entry, type, priority, 0);
}
int at(unsigned index)
{
    struct bootentry_s *entry;
    hlist_for_each_entry(entry, &list, node) {
        if (!index--)
            return container_of(entry, struct record, entry)->id;
    }
    return -1;
}
int valid(void)
{
    unsigned n = 0;
    struct hlist_node **previous = &list.first;
    for (struct hlist_node *node = list.first; node; node = node->next) {
        if (node->pprev != previous || *previous != node || n++ >= 256)
            return 0;
        previous = &node->next;
    }
    return 1;
}
int pointer_ok(unsigned id)
{
    return records[id].entry.drive == &drives[id]
        && sizeof(records[id].entry.drive) == sizeof(void*)
        && (unsigned long)records[id].entry.drive > 0xffffffffUL
        && records[id].entry.description[0] == '?';
}
void select_first(unsigned id)
{
    bootlist_move_first(&list, &records[id].entry);
}
void remove_entry(unsigned id) { hlist_del(&records[id].entry.node); }
'''

ADAPTER_FIXTURE = r'''
#include "block.h"
#include "romfile.h"
#include "arm64/include/bootorder.h"
#include "arm64/include/arm64.h"
#include <stdio.h>
#include <string.h>

static const char *order;
static struct drive_s drives[ARM64_MAX_BLOCK_DRIVES];
static int visited[16], visits;
static int copy_order(struct romfile_s *file, void *out, u32 size)
{
    if (size < file->size) return -1;
    memcpy(out, order, file->size);
    return file->size;
}
struct romfile_s *romfile_find(const char *name)
{
    static struct romfile_s file;
    if (!order || strcmp(name, "bootorder")) return 0;
    file.size = strlen(order) + 1;
    file.copy = copy_order;
    return &file;
}
struct drive_s *arm64_block_drive_at(uint32_t disk)
{
    return disk < ARM64_MAX_BLOCK_DRIVES ? &drives[disk] : 0;
}
void uart_puts(const char *s) { fputs(s, stdout); }
static int attempt(void *ctx)
{
    visited[visits++] = *(int*)ctx;
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *mode = argv[1];
    if (!strcmp(mode, "late") || !strcmp(mode, "late-strict"))
        order = "/missing1\n/missing2\n/missing3\n/missing4\n/missing5\n"
                "/missing6\n/missing7\n/missing8\n/missing9\n/missing10\n"
                "/disk1\n";
    if (!strcmp(mode, "late-strict"))
        order = "/missing1\n/missing2\n/missing3\n/missing4\n/missing5\n"
                "/missing6\n/missing7\n/missing8\n/missing9\n/missing10\n"
                "/disk1\nHALT\n";
    else if (!strcmp(mode, "strict")) order = "/disk1\nHALT\n";
    else if (!strcmp(mode, "empty-strict")) order = "/missing\nHALT\n";
    else if (!strcmp(mode, "halt-first")) order = "HALT\n/disk1\n";
    else if (!strcmp(mode, "halt-middle")) order = "/disk1\nHALT\n/disk0\n";
    else if (!strcmp(mode, "ordered")) order = "/disk1\n/disk0\n";
    arm64_boot_set_disk_path_at(0, "/disk0");
    arm64_boot_set_disk_path_at(1, "/disk1");
    drives[0].type = drives[1].type = DTYPE_VIRTIO_BLK;
    drives[0].cntl_id = 20;
    drives[1].cntl_id = 10;
    int ids[] = {0, 1, 2, 3};
    struct arm64_boot_target targets[4] = {0};
    for (unsigned i = 0; i < 4; i++) {
        targets[i].name = "test candidate";
        targets[i].priority = i == 2 ? 10 : 3;
        targets[i].attempt = attempt;
        targets[i].context = &ids[i];
        if (i != 2)
            targets[i].firmware_path = arm64_boot_disk_path(i == 0 ? 0 : 1);
        arm64_boot_register(&targets[i]);
    }
    if (targets[0].entry.drive != &drives[0] ||
        targets[1].entry.drive != &drives[1]) return 3;
    // The second target on disk1 has an identical key, so it remains stable.
    arm64_boot_execute();
    fputs("ORDER:", stdout);
    for (int i = 0; i < visits; i++) printf(" %d", visited[i]);
    putchar('\n');
    visits = 0;
    arm64_boot_execute();
    fputs("RETRY:", stdout);
    for (int i = 0; i < visits; i++) printf(" %d", visited[i]);
    putchar('\n');
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        common = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-DCONFIG_AARCH64=1', '-I' + str(ROOT / 'src'),
                  '-I' + str(ROOT), '-I' + str(ROOT / 'arm64')]
        shared = [str(ROOT / 'src/bootlist.c'), str(ROOT / 'src/bootprio.c')]
        fixture = temp / 'list.c'
        fixture.write_text(LIST_FIXTURE)
        libpath = temp / 'list.so'
        subprocess.run(common + ['-shared', '-fPIC', str(fixture), *shared,
                                 '-o', str(libpath)], check=True)
        lib = ctypes.CDLL(str(libpath))
        lib.add.argtypes = (ctypes.c_uint, ctypes.c_int, ctypes.c_int,
                            ctypes.c_uint, ctypes.c_uint)
        lib.at.argtypes = (ctypes.c_uint,)
        lib.pointer_ok.argtypes = (ctypes.c_uint,)
        lib.select_first.argtypes = (ctypes.c_uint,)
        lib.remove_entry.argtypes = (ctypes.c_uint,)
        count = 0
        rng = random.Random(0x5EAB105)
        for trial in range(25):
            lib.reset()
            records = []
            for i in range(96):
                kind = rng.choice([1, 2, 3, 0x20, 0x80, 0x81, 0xf0])
                priority = rng.choice([-1, 0, 1, 3, 11, 103, 9999])
                driver = rng.choice([0x20, 0x61, 0x70])
                controller = rng.randrange(4)
                lib.add(i, kind, priority, driver, controller)
                key = (priority, kind, driver if kind <= 3 else 0,
                       controller if kind <= 3 else 0)
                records.append((key, i))
                assert lib.pointer_ok(i), (trial, i)
            expected = [i for _, i in sorted(records, key=lambda item: item[0])]
            actual = [lib.at(i) for i in range(len(records))]
            assert actual == expected, (trial, actual, expected)
            assert lib.at(len(records)) == -1 and lib.valid()
            chosen = expected[-1]
            lib.select_first(chosen)
            expected = [chosen, *expected[:-1]]
            assert [lib.at(i) for i in range(len(records))] == expected
            assert lib.valid()
            # Delete head, middle and tail: verify real hlist backlink updates.
            for removed in [expected[0], expected[len(expected) // 2], expected[-1]]:
                lib.remove_entry(removed)
                expected.remove(removed)
                assert [lib.at(i) for i in range(len(expected))] == expected
                assert lib.valid()
            count += len(records)

        # A disk and HALT at the same rank follow the original IPL type rule.
        lib.reset()
        lib.add(0, 0xf0, 2, 0, 0)
        lib.add(1, 2, 2, 0x61, 0)
        assert [lib.at(i) for i in range(2)] == [1, 0] and lib.valid()

        adapter = temp / 'adapter.c'
        adapter.write_text(ADAPTER_FIXTURE)
        executable = temp / 'adapter'
        subprocess.run(common + [str(adapter), str(ROOT / 'arm64/bootorder.c'),
                                 *shared, '-o', str(executable)], check=True)
        scenarios = {
            'fallback': [1, 3, 0, 2],  # Controller10 before20, disk1 stable.
            'late': [1, 3, 0, 2],      # Rank11 still precedes fallback/diagnostic.
            'late-strict': [1, 3],     # A late HALT must not allow early fallbacks.
            'strict': [1, 3],          # HALT excludes unlisted disk0/diagnostic.
            'empty-strict': [],        # Unmatched entries may not evade HALT.
            'halt-first': [],
            'halt-middle': [1, 3],     # Explicit entry after HALT is also stopped.
            'ordered': [1, 3, 0, 2],
        }
        for mode, expected in scenarios.items():
            output = subprocess.check_output([str(executable), mode], text=True)
            line = next(line for line in output.splitlines() if line.startswith('ORDER:'))
            actual = [int(value) for value in line[6:].split()]
            assert actual == expected, (mode, actual, expected, output)
            retry = next(line for line in output.splitlines() if line.startswith('RETRY:'))
            assert [int(value) for value in retry[6:].split()] == expected, output
            if 'strict' in mode or mode.startswith('halt-'):
                assert 'strict SeaBIOS bootorder HALT reached' in output
        print(f'PASS: {count} shared IPL records; stable disk/controller sorting, '
              f'native pointers, menu/list operations and {len(scenarios)} ARM64 '
              'bootorder/HALT scenarios')


if __name__ == '__main__':
    main()
