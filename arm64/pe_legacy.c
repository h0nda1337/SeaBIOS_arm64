/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Import-free, relocatable native PE32+ ARM64 stage loader. NO EFI. */
#include "include/arm64.h"
#include "include/pe_legacy.h"
#define MAX_PE_BYTES (4u * 1024u * 1024u)
#define IMAGE_FILE_MACHINE_ARM64 0xaa64u
#define IMAGE_SUBSYSTEM_NATIVE 1u
#define IMAGE_SCN_MEM_EXECUTE 0x20000000u
#define IMAGE_REL_BASED_ABSOLUTE 0u
#define IMAGE_REL_BASED_DIR64 10u
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static int bounds(uint32_t at, uint32_t amount, uint32_t size)
{
    return at <= size && amount <= size - at;
}
static void wr64(uint8_t *p, uint64_t n)
{
    for (unsigned i = 0; i < 8; ++i) {
        p[i] = (uint8_t)(n & 0xffu);
        n >>= 8;
    }
}

/* PE base relocation blocks: IMAGE_BASE_RELOCATION + 16-bit type/offsets.
 * Only the architecture-independent DIR64 relocation supported on ARM64 is
 * understood. Never apply an unknown relocation type to executable memory.
 * This loader intentionally does not resolve imports or establish a Windows
 * process environment. A relocatable native PE is NOT a Windows loader. */
static int apply_dir64_relocations(uint8_t *image, uint32_t image_size,
                                   uint32_t rva, uint32_t bytes,
                                   uint64_t preferred_base)
{
    if (!bytes) return preferred_base == PE_LEGACY_BASE ? 0 : -1;
    if (!bounds(rva, bytes, image_size)) return -2;
    uint32_t offset = 0;
    uint32_t patches = 0;
    while (offset < bytes) {
        if (bytes - offset < 8u) return -3;
        const uint8_t *block = image + rva + offset;
        uint32_t page = le32(block);
        uint32_t len = le32(block + 4);
        if (len < 8u || len > bytes - offset || (len & 1u) ||
            (page & 4095u)) return -4;
        for (uint32_t i = 8; i < len; i += 2) {
            uint32_t reloc = le16(block + i);
            uint32_t type = reloc >> 12;
            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (type != IMAGE_REL_BASED_DIR64) return -5;
            uint32_t intra = reloc & 0xfffu;
            if (page > image_size || intra > image_size - page ||
                !bounds(page + intra, 8u, image_size) ||
                ((page + intra) & 7u)) return -6;
            uint8_t *target = image + page + intra;
            uint64_t value = le64(target);
            /* Avoid wrapping arbitrary pointers. This stage loader only
             * relocates pointers INTO the image, not Windows imports. */
            if (value < preferred_base ||
                value - preferred_base >= image_size) return -7;
            wr64(target, PE_LEGACY_BASE + (value - preferred_base));
            if (++patches > (MAX_PE_BYTES / 8u)) return -8;
        }
        offset += len;
    }
    return preferred_base == PE_LEGACY_BASE || patches ? 0 : -9;
}


int arm64_pe_legacy_map(const uint8_t *file, uint32_t size,
                        uint64_t *entry, uint32_t *mapped_size)
{
    if (size < 0x100 || file[0] != 'M' || file[1] != 'Z') return -1;
    uint32_t peoff = le32(file + 0x3c);
    if (!bounds(peoff, 24, size) || le32(file + peoff) != 0x00004550u)
        return -2;
    const uint8_t *coff = file + peoff + 4;
    uint32_t nsec = le16(coff + 2), optsize = le16(coff + 16);
    if (le16(coff) != IMAGE_FILE_MACHINE_ARM64 || !nsec || nsec > 16 ||
        optsize < 112 || !bounds(peoff + 24, optsize, size))
        return -3;
    if (le16(coff + 18) & 0x2000u) return -3; /* PE DLL, not a stage */
    const uint8_t *opt = file + peoff + 24;
    if (le16(opt) != 0x20bu || le16(opt + 68) != IMAGE_SUBSYSTEM_NATIVE) {
        uart_puts("[legacy] PE subsystem is not native ARM64 (Windows EFI images unsupported)\n");
        return -4;
    }
    uint32_t entry_rva = le32(opt + 16), sec_align = le32(opt + 32);
    uint32_t image_size = le32(opt + 56), hdr_size = le32(opt + 60);
    uint32_t dirs = le32(opt + 108);
    uint64_t image_base = le64(opt + 24);
    if (!image_base || image_base > UINT64_MAX - image_size ||
        sec_align < 512 ||
        (sec_align & (sec_align - 1u)) || image_size < 4096 ||
        image_size > MAX_PE_BYTES || hdr_size > size || hdr_size > image_size ||
        entry_rva >= image_size || !arm64_post_can_load(PE_LEGACY_BASE, image_size))
        return -5;
    /* No import resolver, relocation engine, delay imports, TLS callbacks,
     * or managed runtime. Deliberately reject these, never branch blindly. */
    const uint8_t dirs_to_reject[] = {1, 9, 13, 14};
    for (unsigned i = 0; i < sizeof(dirs_to_reject); ++i) {
        uint32_t d = dirs_to_reject[i];
        if (dirs > d) {
            if (optsize < 112 + 8 * (d + 1)) return -6;
            if (le32(opt + 112 + 8*d) || le32(opt + 116 + 8*d)) return -7;
        }
    }
    /* IMAGE_DIRECTORY_ENTRY_BASERELOC (5). Require both fields together.
     * Reject a moved image without relocations rather than launching it at
     * the wrong address; accept fixed-base native images as before. */
    uint32_t reloc_rva = 0, reloc_size = 0;
    if (dirs > 5u) {
        if (optsize < 112u + 8u * 6u) return -6;
        reloc_rva = le32(opt + 112 + 8 * 5);
        reloc_size = le32(opt + 116 + 8 * 5);
    }
    if (!!reloc_rva != !!reloc_size ||
        (image_base != PE_LEGACY_BASE && !reloc_size) ||
        (reloc_size && !bounds(reloc_rva, reloc_size, image_size)))
        return -12;
    uint32_t sections = peoff + 24 + optsize;
    if (!bounds(sections, nsec * 40, size) ||
        !bounds(sections, nsec * 40, hdr_size)) return -8;
    int executable = 0;
    for (uint32_t i = 0; i < nsec; ++i) {
        const uint8_t *sec = file + sections + i * 40;
        uint32_t vsize = le32(sec + 8), vaddr = le32(sec + 12);
        uint32_t rawsize = le32(sec + 16), rawptr = le32(sec + 20);
        uint32_t span = vsize > rawsize ? vsize : rawsize;
        if (!bounds(vaddr, span, image_size) ||
            (rawsize && !bounds(rawptr, rawsize, size)) ||
            (span && vaddr < hdr_size) || (vaddr & (sec_align - 1u)))
            return -9;
        for (uint32_t j = 0; j < i; ++j) {
            const uint8_t *prev = file + sections + j*40;
            uint32_t a = le32(prev + 12), asize = le32(prev + 8);
            uint32_t araw = le32(prev + 16);
            if (asize < araw) asize = araw;
            if (span && asize && vaddr < a + asize && a < vaddr + span)
                return -10;
        }
        if ((le32(sec + 36) & IMAGE_SCN_MEM_EXECUTE) &&
            entry_rva >= vaddr && entry_rva - vaddr < rawsize)
            executable = 1;
    }
    if (!executable) return -11;
    /* Validation above is complete; only now write to executable RAM. */
    uint8_t *image = (uint8_t *)(uintptr_t)PE_LEGACY_BASE;
    for (uint32_t i = 0; i < image_size; ++i) image[i] = 0;
    for (uint32_t i = 0; i < hdr_size; ++i) image[i] = file[i];
    for (uint32_t s = 0; s < nsec; ++s) {
        const uint8_t *sec = file + sections + s * 40;
        uint32_t src = le32(sec + 20), dest = le32(sec + 12);
        for (uint32_t i = 0; i < le32(sec + 16); ++i)
            image[dest + i] = file[src + i];
    }
    if (apply_dir64_relocations(image, image_size, reloc_rva,
                                reloc_size, image_base)) return -13;
    *entry = PE_LEGACY_BASE + entry_rva;
    *mapped_size = image_size;
    return 0;
}
