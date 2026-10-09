/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"

/* Minimal bounded flattened-device-tree reader for QEMU virt. */
#define FDT_MAGIC     0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u
#define FDT_MAX_BYTES  (2u * 1024u * 1024u)
#define FDT_MAX_DEPTH  16

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int range_valid(uint32_t offset, uint32_t count, uint32_t limit)
{
    return offset <= limit && count <= limit - offset;
}

static int streq_bounded(const unsigned char *p, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    while (i < n && lit[i] && p[i] == (unsigned char)lit[i])
        ++i;
    return i < n && p[i] == 0 && lit[i] == 0;
}

/* Compatible is a list of zero-terminated strings. */
static int compatible_has(const unsigned char *p, uint32_t len, const char *match)
{
    uint32_t i = 0;
    while (i < len) {
        uint32_t left = len - i;
        uint32_t j = 0;
        while (j < left && p[i + j] != 0)
            ++j;
        if (j == left)
            return 0;
        if (streq_bounded(p + i, j + 1, match))
            return 1;
        i += j + 1;
    }
    return 0;
}

static uint64_t read_cells(const unsigned char *p, uint32_t count)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < count; ++i)
        x = (x << 32) | be32(p + 4 * i);
    return x;
}

struct node_state {
    const unsigned char *reg;
    uint32_t reg_len;
    int is_mem;
    int is_uart;
    int is_fwcfg;
    int is_virtio;
    int is_reserved_parent;
    int disabled;
    uint32_t address_cells;
    uint32_t size_cells;
};

int fdt_probe(const void *dtb, struct fdt_info *result)
{
    if (!dtb || !result)
        return -1;
    const unsigned char *d = (const unsigned char *)dtb;
    if (be32(d) != FDT_MAGIC)
        return -2;
    uint32_t total = be32(d + 4);
    if (total < 40 || total > FDT_MAX_BYTES)
        return -3;
    uint32_t off_struct = be32(d + 8), off_strings = be32(d + 12);
    uint32_t size_strings = be32(d + 32), size_struct = be32(d + 36);
    if (!range_valid(off_struct, size_struct, total) ||
        !range_valid(off_strings, size_strings, total) ||
        !size_struct || !size_strings || (off_struct & 3))
        return -4;

    const unsigned char *s = d + off_struct;
    const unsigned char *strings = d + off_strings;
    uint32_t pos = 0;
    int depth = -1;
    struct node_state nodes[FDT_MAX_DEPTH] = {0};
    uint32_t ac = 2, sc = 2; /* standard defaults until root properties */
    result->has_ram = result->has_uart = 0;
    result->ram_base = result->ram_size = result->uart_base = 0;
    result->fwcfg_base = 0;
    result->has_fwcfg = 0;
    result->dtb_bytes = total;
    result->ram_count = 0;
    result->reserved_count = 0;
    result->virtio_count = 0;

    while (range_valid(pos, 4, size_struct)) {
        uint32_t op = be32(s + pos);
        pos += 4;
        if (op == FDT_BEGIN_NODE) {
            if (depth + 1 >= FDT_MAX_DEPTH)
                return -5;
            ++depth;
            nodes[depth] = (struct node_state){0};
            uint32_t start = pos;
            while (pos < size_struct && s[pos])
                ++pos;
            if (pos >= size_struct)
                return -6;
            if (depth == 1 && streq_bounded(s + start, pos - start + 1, "memory"))
                nodes[depth].is_mem = 1;
            if (depth == 1 && streq_bounded(s + start, pos - start + 1,
                                           "reserved-memory")) {
                nodes[depth].is_reserved_parent = 1;
                nodes[depth].address_cells = ac;
                nodes[depth].size_cells = sc;
            }
            ++pos;
            pos = (pos + 3u) & ~3u;
            if (pos > size_struct)
                return -7;
        } else if (op == FDT_PROP) {
            if (depth < 0 || !range_valid(pos, 8, size_struct))
                return -8;
            uint32_t len = be32(s + pos), nameoff = be32(s + pos + 4);
            pos += 8;
            if (!range_valid(pos, len, size_struct) || nameoff >= size_strings)
                return -9;
            uint32_t k = nameoff;
            while (k < size_strings && strings[k])
                ++k;
            if (k == size_strings)
                return -10;
            const unsigned char *prop = s + pos;
            uint32_t propnamelen = k - nameoff + 1;
            const unsigned char *name = strings + nameoff;
            if (depth == 0 && len == 4 &&
                streq_bounded(name, propnamelen, "#address-cells"))
                ac = be32(prop);
            if (depth == 0 && len == 4 &&
                streq_bounded(name, propnamelen, "#size-cells"))
                sc = be32(prop);
            if (depth == 1 && nodes[depth].is_reserved_parent && len == 4) {
                if (streq_bounded(name, propnamelen, "#address-cells"))
                    nodes[depth].address_cells = be32(prop);
                if (streq_bounded(name, propnamelen, "#size-cells"))
                    nodes[depth].size_cells = be32(prop);
            }
            if (depth == 2 && nodes[depth-1].is_reserved_parent &&
                streq_bounded(name, propnamelen, "status") &&
                streq_bounded(prop, len, "disabled"))
                nodes[depth].disabled = 1;
            if (depth == 1 && streq_bounded(name, propnamelen, "device_type") &&
                streq_bounded(prop, len, "memory"))
                nodes[depth].is_mem = 1;
            if (depth == 1 && streq_bounded(name, propnamelen, "compatible")) {
                if (compatible_has(prop, len, "arm,pl011"))
                    nodes[depth].is_uart = 1;
                if (compatible_has(prop, len, "qemu,fw-cfg-mmio"))
                    nodes[depth].is_fwcfg = 1;
                if (compatible_has(prop, len, "virtio,mmio"))
                    nodes[depth].is_virtio = 1;
            }
            if ((depth == 1 || (depth == 2 &&
                 nodes[depth - 1].is_reserved_parent)) &&
                streq_bounded(name, propnamelen, "reg")) {
                nodes[depth].reg = prop;
                nodes[depth].reg_len = len;
            }
            pos += len;
            pos = (pos + 3u) & ~3u;
            if (pos > size_struct)
                return -11;
        } else if (op == FDT_END_NODE) {
            if (depth < 0)
                return -12;
            struct node_state *n = &nodes[depth];
            const int reserved_child = depth == 2 &&
                nodes[depth - 1].is_reserved_parent;
            uint32_t node_ac = reserved_child ?
                nodes[depth - 1].address_cells : ac;
            uint32_t node_sc = reserved_child ?
                nodes[depth - 1].size_cells : sc;
            if (n->reg && (depth == 1 || reserved_child)) {
                if ((node_ac != 1 && node_ac != 2) ||
                    (node_sc != 1 && node_sc != 2))
                    return -16;
                uint32_t stride = 4u * (node_ac + node_sc);
                if (n->reg_len % stride)
                    return -17;
                for (uint32_t index = 0; index < n->reg_len;
                     index += stride) {
                    uint64_t base = read_cells(n->reg + index, node_ac);
                    uint64_t bytes = read_cells(n->reg + index +
                                               node_ac * 4u, node_sc);
                    if (!bytes || base > UINT64_MAX - bytes)
                        return -18;
                    if (depth == 1 && n->is_mem) {
                        if (result->ram_count >= ARM64_MAX_RAM_BANKS)
                            return -19;
                        result->ram[result->ram_count++] =
                            (struct fdt_region){ base, bytes };
                        if (!result->has_ram) {
                            result->ram_base = base;
                            result->ram_size = bytes;
                            result->has_ram = 1;
                        }
                    }
                    if (reserved_child && !n->disabled) {
                        if (result->reserved_count >= ARM64_MAX_RESERVED_REGIONS)
                            return -20;
                        result->reserved[result->reserved_count++] =
                            (struct fdt_region){ base, bytes };
                    }
                    if (depth == 1 && n->is_virtio && bytes >= 0x100u) {
                        if (result->virtio_count >= ARM64_MAX_VIRTIO_MMIO)
                            return -21;
                        result->virtio_mmio[result->virtio_count++] =
                            (struct fdt_region){ base, bytes };
                    }
                    if (depth == 1 && n->is_uart && bytes >= 0x34u &&
                        !result->has_uart) {
                        result->uart_base = base;
                        result->has_uart = 1;
                    }
                    if (depth == 1 && n->is_fwcfg && bytes >= 0x18u &&
                        !result->has_fwcfg) {
                        result->fwcfg_base = base;
                        result->has_fwcfg = 1;
                    }
                }
            }
            --depth;
        } else if (op == FDT_END) {
            return depth == -1 ? 0 : -13;
        } else if (op != FDT_NOP) {
            return -14;
        }
    }
    return -15;
}

/* Device Tree memreserve entries are 16-byte big-endian address/length
 * pairs terminated by 0,0. Reject malformed layouts rather than walk
 * outside the declared DTB buffer. */
static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

int fdt_for_each_reservation(const void *dtb,
                             fdt_reservation_fn fn, void *ctx)
{
    if (!dtb || !fn)
        return -1;
    const unsigned char *d = dtb;
    if (be32(d) != FDT_MAGIC)
        return -2;
    uint32_t total = be32(d + 4);
    if (total < 56 || total > FDT_MAX_BYTES)
        return -3;
    uint32_t off = be32(d + 16);
    uint32_t struct_off = be32(d + 8);
    if (off < 40 || (off & 7u) || struct_off > total ||
        off > struct_off || struct_off - off < 16)
        return -4;
    int reservations = 0;
    while (off <= struct_off - 16) {
        uint64_t address = be64(d + off);
        uint64_t size = be64(d + off + 8);
        off += 16;
        if (address == 0 && size == 0)
            return reservations;
        if (!size || address > UINT64_MAX - size)
            return -5;
        int status = fn(address, size, ctx);
        if (status)
            return status;
        ++reservations;
    }
    return -6;
}
