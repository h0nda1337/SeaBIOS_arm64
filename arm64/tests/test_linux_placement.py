#!/usr/bin/env python3
"""Host regressions for actual Linux loader placement and romfile policy.

Compile the production loader in a host driver, discarding unused disk/handoff
sections. The memory-range predicate comes from production arm64/post.c and
the configuration lookup uses production src/romfile.c.
"""
import subprocess
import tempfile
from pathlib import Path

ARM = Path(__file__).resolve().parents[1]
ROOT = ARM.parent
DRIVER = r'''
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "linuxboot.c"
struct e820entry e820_list[8];
int e820_count;
void uart_puts(const char *s) { (void)s; }
void uart_hex(uint64_t v) { (void)v; }
void uart_newline(void) {}
void arm64_debug_sink(int l, const char *s, ...) { (void)l; (void)s; }
static const char *Config;
static int config_copy(struct romfile_s *file, void *out, uint32_t size)
{
    assert(file->size <= size);
    memcpy(out, Config, file->size);
    return file->size;
}
static void bank(int index, uint64_t start, uint64_t size, uint32_t type)
{
    e820_list[index] = (struct e820entry){start, size, type};
}
int main(void)
{
    struct linux_candidate candidate = {0};
    candidate.header.text_offset = 0x80000;
    candidate.header.image_size = 32u * 1024u * 1024u;
    candidate.from_fat32 = 1;
    candidate.initrd_file_bytes = 2u * 1024u * 1024u;
    e820_count = 1;
    bank(0, 0x40000000, 512u * 1024u * 1024u, E820_RAM);
    assert(linux_load_address(&candidate) == 0x48080000);
    /* Smaller RAM and a reserved firmware/DTB area select aligned free RAM. */
    e820_count = 3;
    bank(0, 0x40000000, 0x436000, E820_RESERVED);
    bank(1, 0x40436000, 0x3bca000, E820_RAM);
    bank(2, 0x44000000, 0x4000000, E820_RESERVED);
    assert(linux_load_address(&candidate) == 0x40680000);
    /* A kernel alone fits the traditional base; its initrd does not.
     * Reject that placement before I/O and choose a bank fitting both. */
    bank(0, 0x48000000, 0x2080000, E820_RAM);
    bank(1, 0x50000000, 0x4000000, E820_RAM);
    e820_count = 2;
    assert(linux_load_address(&candidate) == 0x50080000);
    e820_count = 1;
    bank(0, 0x40000000, 0x100000, E820_RAM);
    assert(linux_load_address(&candidate) == 0);
    /* Overflowing bases are rejected even before memory-map lookup. */
    assert(linux_region(&candidate, UINT64_MAX - 4) == 0);
    bank(0, UINT64_MAX - 4, 4, E820_RAM);
    assert(linux_load_address(&candidate) == 0);

    assert(strcmp(linux_cmdline(), LINUX_CMDLINE) == 0);
    struct romfile_s file = {0};
    memcpy(file.name, "etc/linux-cmdline", sizeof("etc/linux-cmdline"));
    file.copy = config_copy;
    romfile_add(&file);
    Config = "root=/dev/vda2 rw\r\n";
    file.size = strlen(Config);
    assert(strcmp(linux_cmdline(), "root=/dev/vda2 rw") == 0);
    Config = "root=/dev/vda2\0hidden";
    file.size = 20;
    assert(linux_cmdline() == NULL);
    Config = "\n";
    file.size = 1;
    assert(linux_cmdline() == NULL);
    file.size = 256;
    assert(linux_cmdline() == NULL);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    directory = Path(td)
    driver, binary = directory / 'driver.c', directory / 'test'
    driver.write_text(DRIVER)
    subprocess.run(['gcc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-DCONFIG_AARCH64=1', '-ffunction-sections',
                    '-fdata-sections', '-I' + str(ARM),
                    '-I' + str(ARM / 'include'), '-I' + str(ROOT / 'src'),
                    str(driver), str(ARM / 'post.c'),
                    str(ROOT / 'src/romfile.c'), '-Wl,--gc-sections',
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: safe kernel/initrd placement, reservations, overflow and romfile cmdline')
