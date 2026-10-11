/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "include/arm64.h"
#include "include/bootorder.h"
#include "include/fwcfg.h"
#include "../src/postflow.h"

/* ARM64 staging firmware - not yet a UEFI or legacy BIOS implementation. */
#define PAYLOAD_MAGIC "SBARMP01"
#define PAYLOAD_HEADER_SIZE 64u

static uint32_t u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t fnv1a32(const unsigned char *bytes, uint32_t count)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < count; i++) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

/* Payload format: magic[8], code_length[4], FNV1a[4], reserved[48], code[].
 * The checksum is for catching accidental corruption, NOT authentication. */
struct diagnostic_context {
    const struct fdt_info *info;
    void *dtb;
};

static int try_payload(void *context)
{
    const struct diagnostic_context *ctx = context;
    const struct fdt_info *info = ctx->info;
    void *dtb = ctx->dtb;
    /* The original SeaBIOS E820 map describes all RAM banks and firmware
     * reservations. Do not assume the first FDT memory node contains the
     * payload (e.g. an emulated machine can expose discontiguous RAM). */
    if (!info->has_ram)
        return -1;
    if (!arm64_post_can_load(VIRT_PAYLOAD_BASE, VIRT_PAYLOAD_MAX))
        return -4;
    const unsigned char *header = (const void *)(uintptr_t)VIRT_PAYLOAD_BASE;
    const char *magic = PAYLOAD_MAGIC;
    for (unsigned i = 0; i < 8; i++)
        if (header[i] != (unsigned char)magic[i])
            return 0;
    uint32_t len = u32le(header + 8);
    uint32_t expected = u32le(header + 12);
    if (len == 0 || len > VIRT_PAYLOAD_MAX - PAYLOAD_HEADER_SIZE)
        return -2;
    const unsigned char *code = header + PAYLOAD_HEADER_SIZE;
    if (fnv1a32(code, len) != expected)
        return -3;
    arm64_post_reserve_payload(VIRT_PAYLOAD_BASE,
                               PAYLOAD_HEADER_SIZE + len);
    uart_puts("[arm64] valid diagnostic payload -> branch ");
    uart_hex((uintptr_t)code);
    uart_newline();
    arm64_cache_clean_range(code, len);
    arm64_handoff((void *)code, dtb);
}

struct arm64_post_context {
    struct fdt_info *info;
    void *dtb;
    struct diagnostic_context *diagnostic;
    struct arm64_boot_target diagnostic_target;
};

/* Architecture-specific POST operations. The order is defined by the
 * genuine shared SeaBIOS src/postflow.c, not a second ARM-only scheduler. */
static int arm64_post_step(void *opaque, enum postflow_stage stage)
{
    struct arm64_post_context *ctx = opaque;
    struct fdt_info *info = ctx->info;
    switch (stage) {
    case POSTFLOW_INTERFACE:
        if (arm64_post_memory(info, ctx->dtb))
            return -1;
        return 0;
    case POSTFLOW_PLATFORM:
        if (info->has_fwcfg) {
            uart_puts("[arm64] QEMU fw_cfg MMIO: ");
            uart_hex(info->fwcfg_base);
            uart_newline();
            int found = arm64_fwcfg_init(info->fwcfg_base);
            if (found >= 0)
                arm64_fwcfg_report();
            else {
                uart_puts("[arm64] fw_cfg initialization rejected: ");
                uart_dec((uint32_t)(-found));
                uart_newline();
            }
        } else {
            uart_puts("[arm64] no fw_cfg node in Device Tree\n");
        }
        return 0;
    case POSTFLOW_ASYNC_DEVICES:
        /* No threaded device setup until the ARM IRQ/scheduler is ported. */
        return 0;
    case POSTFLOW_VIDEO:
        /* No x86 VGA option ROMs on AArch64; PL011 was initialized early. */
        return 0;
    case POSTFLOW_SYNC_DEVICES: {
        arm64_virtio_discover(info);
        /* QEMU bootorder uses the exact VirtIO-MMIO legacy path format. */
        static char boot_mmio_path[] = "/virtio-mmio@0000000000000000/*";
        uintptr_t disk_mmio = arm64_virtio_blk_mmio();
        if (disk_mmio) {
            static const char hex[] = "0123456789abcdef";
            for (int i = 0; i < 16; ++i)
                boot_mmio_path[13 + i] = hex[(disk_mmio >> ((15-i)*4)) & 15u];
            arm64_boot_set_disk_path(boot_mmio_path);
        }
        arm64_disk_probe();
        arm64_legacy_discover(ctx->dtb);
        arm64_linux_discover(ctx->dtb);
        return 0;
    }
    case POSTFLOW_OPTIONROMS:
        /* Native ARM64 legacy boot does not execute x86 PCI option ROMs. */
        return 0;
    case POSTFLOW_BOOTMENU:
        ctx->diagnostic_target.priority = 10;
        ctx->diagnostic_target.name = "AArch64 QEMU diagnostic payload";
        ctx->diagnostic_target.attempt = try_payload;
        ctx->diagnostic_target.context = ctx->diagnostic;
        ctx->diagnostic_target.firmware_path = NULL;
        arm64_boot_register(&ctx->diagnostic_target);
        return 0;
    case POSTFLOW_PREPARE:
        /* Memory reservations checked by ARM64 loaders before handoff. */
        return 0;
    case POSTFLOW_PROTECT:
        /* No flash protection, MMU or memory permissions provided yet. */
        return 0;
    case POSTFLOW_BOOT:
        uart_puts("[post] shared SeaBIOS POST flow ready\n");
        arm64_boot_execute();
        return 0;
    default:
        return -1;
    }
}

void boot_main(void *dtb)
{
    /* Initial console base is a QEMU virt development default only. */
    uart_init(VIRT_PL011_FALLBACK);
    uart_puts("\nSeaBIOS-ARM64 legacy MBR v1.2-dev (AArch64/QEMU virt)\n");
    arm64_report_cpu();
    arm64_timer_init();
    struct fdt_info info;
    int status = fdt_probe(dtb, &info);
    if (status != 0) {
        uart_puts("[arm64] invalid DTB, parser status: ");
        uart_dec((uint32_t)(-status));
        uart_newline();
        goto idle;
    }
    uart_puts("[arm64] Device Tree bytes: ");
    uart_dec(info.dtb_bytes);
    uart_newline();
    if (info.has_uart) {
        if (info.uart_base != VIRT_PL011_FALLBACK)
            uart_init(info.uart_base);
        uart_puts("[arm64] PL011 UART: ");
        uart_hex(info.uart_base);
        uart_newline();
    } else {
        uart_puts("[arm64] UART missing in DTB; using QEMU early console\n");
    }
    if (info.has_ram) {
        uart_puts("[arm64] RAM start: ");
        uart_hex(info.ram_base);
        uart_puts(" / size: ");
        uart_hex(info.ram_size);
        uart_newline();
    } else {
        uart_puts("[arm64] no RAM node found\n");
    }

    /* The shared postflow_run() sequence is also used by src/post.c on x86.
     * The ARM64 callbacks deliberately skip x86-only stages. */
    struct diagnostic_context context = { .info = &info, .dtb = dtb };
    struct arm64_post_context post_ctx = {
        .info = &info, .dtb = dtb, .diagnostic = &context,
    };
    const struct postflow_ops post_ops = { .step = arm64_post_step };
    enum postflow_stage failed_stage;
    status = postflow_run(&post_ops, &post_ctx, &failed_stage);
    if (status) {
        uart_puts("[post] shared POST stage failed: ");
        uart_dec((uint32_t)failed_stage);
        uart_puts(" status ");
        uart_dec((uint32_t)(-status));
        uart_newline();
    }

idle:
    uart_puts("[arm64] halt (WFE). Windows ARM64 legacy boot not implemented.\n");
    for (;;)
        __asm__ volatile("wfe");
}
