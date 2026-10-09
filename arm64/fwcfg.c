/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/fwcfg.h"
#include "../src/romfile.h"
#include "include/seabios_compat.h"

/* QEMU fw_cfg MMIO: data (8-bit), selector (big-endian 16-bit).
 * Directory format: be32 count, repeated be32 size, be16 key, be16 reserved,
 * 56-byte NUL-terminated filename. No DMA needed for firmware config files.
 * This is a new ARM backend for the unmodified romfile registry. */
#define FWCFG_SELECT 0x08u
#define FWCFG_DIRECTORY 0x19u
#define FWCFG_FILE_ENTRIES 48u
#define FWCFG_MAX_DIRECTORY 4096u
#define FWCFG_MAX_FILE_SIZE (1024u * 1024u)

struct arm64_cfg_file {
    struct romfile_s file;
    uint16_t key;
};

static uintptr_t fwcfg_base;
static struct arm64_cfg_file Files[FWCFG_FILE_ENTRIES];
static unsigned file_count;

static uint16_t load_be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t load_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void select_item(uint16_t key)
{
    *(volatile uint16_t *)(fwcfg_base + FWCFG_SELECT) =
        (uint16_t)((key >> 8) | (key << 8));
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint8_t read_byte(void)
{
    return *(volatile uint8_t *)fwcfg_base;
}

static int copy_file(struct romfile_s *file, void *dst, uint32_t maxlen)
{
    if (file->size > maxlen || file->size > FWCFG_MAX_FILE_SIZE)
        return -1;
    struct arm64_cfg_file *entry =
        container_of(file, struct arm64_cfg_file, file);
    select_item(entry->key);
    unsigned char *out = dst;
    for (uint32_t i = 0; i < file->size; ++i)
        out[i] = read_byte();
    return (int)file->size;
}

int arm64_fwcfg_init(uint64_t base)
{
    if (!base || (base & 1u) || base > (uint64_t)UINTPTR_MAX - 0x0au)
        return -1;
    fwcfg_base = (uintptr_t)base;
    select_item(0); /* FW_CFG_SIGNATURE */
    if (read_byte() != 'Q' || read_byte() != 'E' ||
        read_byte() != 'M' || read_byte() != 'U')
        return -2;

    select_item(FWCFG_DIRECTORY);
    uint8_t header[4];
    for (unsigned i = 0; i < 4; ++i)
        header[i] = read_byte();
    uint32_t count = load_be32(header);
    if (count > FWCFG_MAX_DIRECTORY)
        return -3;
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t raw[64];
        for (unsigned i = 0; i < sizeof(raw); ++i)
            raw[i] = read_byte();
        uint32_t size = load_be32(raw);
        uint16_t key = load_be16(raw + 4);
        if (!size || size > FWCFG_MAX_FILE_SIZE ||
            file_count >= FWCFG_FILE_ENTRIES)
            continue;
        unsigned n = 0;
        while (n < 56 && raw[8 + n]) ++n;
        if (n == 0 || n == 56)
            continue;
        struct arm64_cfg_file *entry = &Files[file_count++];
        for (unsigned i = 0; i < n; ++i)
            entry->file.name[i] = (char)raw[8 + i];
        entry->file.name[n] = 0;
        entry->file.size = size;
        entry->file.copy = copy_file;
        entry->key = key;
        romfile_add(&entry->file);
    }
    return (int)file_count;
}

void arm64_fwcfg_report(void)
{
    uart_puts("[fwcfg] registered romfiles: ");
    uart_dec(file_count);
    uart_newline();
    /* Enumerate via the original SeaBIOS registry implementation rather
     * than reimplementing file traversal in this ARM-specific backend. */
    struct romfile_s *it = NULL;
    for (unsigned n = 0; n < 8u; ++n) {
        it = romfile_findprefix("", it);
        if (!it)
            break;
        uart_puts("[fwcfg] romfile: ");
        uart_puts(it->name);
        uart_newline();
    }
    /* Uses the actual upstream romfile_loadfile() and boot-time allocator. */
    char *order = romfile_loadfile("bootorder", NULL);
    if (!order) {
        uart_puts("[fwcfg] no 'bootorder' romfile\n");
        return;
    }
    uart_puts("[fwcfg] SeaBIOS bootorder (first 256 bytes):\n");
    for (unsigned i = 0; order[i] && i < 256u; ++i)
        uart_putc(order[i]);
    uart_newline();
    free(order);
}
