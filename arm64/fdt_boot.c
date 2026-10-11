/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/fdt_boot.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* An FDT writer for the early, freestanding AArch64 firmware. This is NOT
 * libfdt: it deliberately handles only well-formed v17 QEMU virt FDTs.
 * No original device node or memreserve entry is deliberately removed. */
#define FDT_MAGIC 0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE 2u
#define FDT_PROP 3u
#define FDT_NOP 4u
#define FDT_END 9u
#define FDT_LIMIT (2u * 1024u * 1024u)
#define FDT_HEADROOM 4096u
#define BOOTARG_MAX 255u
#define FDT_OUTPUT_SIZE (FDT_LIMIT + FDT_HEADROOM)
static uint8_t BootTree[FDT_OUTPUT_SIZE] __attribute__((aligned(4096)));

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}
static void put64(uint8_t *p, uint64_t n)
{
    put32(p, (uint32_t)(n >> 32));
    put32(p + 4, (uint32_t)n);
}
static int valid(uint32_t begin, uint32_t length, uint32_t limit)
{
    return begin <= limit && length <= limit - begin;
}
static int is_name(const uint8_t *p, uint32_t max, const char *lit)
{
    for (uint32_t i = 0; i < max; ++i) {
        if (!lit[i] || p[i] != (uint8_t)lit[i])
            return !p[i] && !lit[i];
    }
    return 0;
}
static uint32_t aligned(uint32_t n) { return (n + 3u) & ~3u; }

struct writer { uint8_t *buf; uint32_t size; uint32_t limit; };
static int append(struct writer *w, const void *data, uint32_t length)
{
    if (!valid(w->size, length, w->limit)) return -1;
    memcpy(w->buf + w->size, data, length);
    w->size += length;
    return 0;
}
static int append32(struct writer *w, uint32_t value)
{
    if (!valid(w->size, 4, w->limit)) return -1;
    put32(w->buf + w->size, value);
    w->size += 4;
    return 0;
}
static int prop(struct writer *w, uint32_t nameoff, const void *p,
                uint32_t length)
{
    uint8_t pad[3] = {0};
    if (append32(w, FDT_PROP) || append32(w, length) ||
        append32(w, nameoff) || append(w, p, length) ||
        append(w, pad, aligned(length) - length)) return -1;
    return 0;
}
static int chosen_props(struct writer *w, uint32_t boot_name,
                        uint32_t start_name, uint32_t end_name,
                        const char *args, uint32_t argsize,
                        uint64_t start, uint64_t end)
{
    uint8_t cell[8];
    if (prop(w, boot_name, args, argsize)) return -1;
    if (start != end) {
        put64(cell, start);
        if (prop(w, start_name, cell, 8)) return -1;
        put64(cell, end);
        if (prop(w, end_name, cell, 8)) return -1;
    }
    return 0;
}

int arm64_fdt_prepare(const void *source, const char *bootargs,
                      uint64_t initrd_start, uint64_t initrd_end,
                      const void **prepared, uint32_t *length)
{
    if (!source || !bootargs || !prepared || !length ||
        initrd_start > initrd_end || (initrd_start == 0 && initrd_end))
        return -1;
    const uint8_t *in = source;
    if (be32(in) != FDT_MAGIC) return -2;
    uint32_t total = be32(in + 4), struct_off = be32(in + 8);
    uint32_t strings_off = be32(in + 12), reserve_off = be32(in + 16);
    uint32_t version = be32(in + 20);
    uint32_t strings_size = be32(in + 32), struct_size = be32(in + 36);
    if (total < 56 || total > FDT_LIMIT || version < 17 ||
        struct_off < 56 || (struct_off & 3u) || (reserve_off & 7u) ||
        reserve_off < 40 || reserve_off >= struct_off ||
        !valid(struct_off, struct_size, total) ||
        !valid(strings_off, strings_size, total) ||
        strings_off < struct_off + struct_size || !strings_size)
        return -3;
    uint32_t argslen = 0;
    while (argslen <= BOOTARG_MAX && bootargs[argslen]) ++argslen;
    if (argslen > BOOTARG_MAX) return -4;
    uint32_t argsize = argslen + 1;
    /* Appending names, even if the original strings table already contains
     * them, is permitted by the FDT format and avoids mutating old offsets. */
    static const char names[] = "bootargs\0linux,initrd-start\0linux,initrd-end";
    uint32_t bo = strings_size;
    uint32_t so = bo + 9u;
    uint32_t eo = so + sizeof("linux,initrd-start");
    struct writer w = {BootTree, struct_off, FDT_OUTPUT_SIZE};
    memcpy(BootTree, in, struct_off);
    const uint8_t *src = in + struct_off;
    const uint8_t *strings = in + strings_off;
    uint32_t pos = 0;
    int depth = -1, chosen_depth = -1, saw_chosen = 0;
    int root_address_cells = 2;
    int saw_end = 0;
    while (valid(pos, 4, struct_size)) {
        uint32_t token = be32(src + pos);
        uint32_t at = pos;
        pos += 4;
        if (token == FDT_BEGIN_NODE) {
            uint32_t name_start = pos;
            while (pos < struct_size && src[pos]) ++pos;
            if (pos >= struct_size) return -5;
            uint32_t name_len = pos - name_start + 1;
            if (depth == 0 && is_name(src + name_start, name_len, "chosen")) {
                if (saw_chosen) return -6;
                saw_chosen = 1;
                chosen_depth = 1;
            }
            ++depth;
            pos = aligned(pos + 1u);
            if (pos > struct_size || append(&w, src + at, pos - at)) return -7;
            /* FDT properties must precede subnodes. Insert immediately
             * after BEGIN_NODE, never after a chosen child. */
            if (depth == chosen_depth &&
                chosen_props(&w, bo, so, eo, bootargs, argsize,
                             initrd_start, initrd_end)) return -14;
        } else if (token == FDT_PROP) {
            if (depth < 0 || !valid(pos, 8, struct_size)) return -8;
            uint32_t payload_len = be32(src + pos);
            uint32_t nameoff = be32(src + pos + 4);
            pos += 8;
            if (!valid(pos, payload_len, struct_size) ||
                nameoff >= strings_size) return -9;
            uint32_t max_name = strings_size - nameoff;
            /* Reject unterminated property names. */
            int terminator = 0;
            for (uint32_t i = 0; i < max_name; ++i)
                if (!strings[nameoff + i]) { terminator = 1; break; }
            if (!terminator) return -10;
            const uint8_t *key = strings + nameoff;
            if (depth == 0 && is_name(key, max_name, "#address-cells") &&
                payload_len == 4)
                root_address_cells = (int)be32(src + pos);
            pos = aligned(pos + payload_len);
            if (pos > struct_size) return -11;
            if (depth == chosen_depth &&
                (is_name(key, max_name, "bootargs") ||
                 is_name(key, max_name, "linux,initrd-start") ||
                 is_name(key, max_name, "linux,initrd-end")))
                continue;
            if (append(&w, src + at, pos - at)) return -12;
        } else if (token == FDT_END_NODE) {
            if (depth < 0) return -13;
            if (depth == chosen_depth) chosen_depth = -1;
            if (depth == 0 && !saw_chosen) {
                static const uint8_t chosen[] = {
                    0, 0, 0, 1, 'c','h','o','s','e','n',0,0
                };
                if (append(&w, chosen, sizeof(chosen)) ||
                    chosen_props(&w, bo, so, eo, bootargs, argsize,
                                 initrd_start, initrd_end) ||
                    append32(&w, FDT_END_NODE)) return -15;
            }
            if (append32(&w, FDT_END_NODE)) return -16;
            --depth;
        } else if (token == FDT_NOP) {
            if (append32(&w, FDT_NOP)) return -17;
        } else if (token == FDT_END) {
            if (depth != -1 || saw_end || append32(&w, FDT_END))
                return -18;
            saw_end = 1;
            break;
        } else return -19;
    }
    if (!saw_end || root_address_cells != 2) return -20;
    uint32_t new_struct = w.size - struct_off;
    uint32_t new_strings = w.size;
    if (append(&w, in + strings_off, strings_size) ||
        append(&w, names, sizeof(names))) return -21;
    put32(BootTree + 4, w.size);
    put32(BootTree + 12, new_strings);
    put32(BootTree + 32, strings_size + sizeof(names));
    put32(BootTree + 36, new_struct);
    *prepared = BootTree;
    *length = w.size;
    return 0;
}
