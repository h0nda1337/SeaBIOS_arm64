/* Copyright the original SeaBIOS contributors. Licensed under LGPLv3.
 * ARM64 transport is compiled from the same src/hw/virtio-mmio.c as x86. */
#if CONFIG_AARCH64
#include "../../arm64/include/arm64.h"
#include "../types.h"
#include "virtio-ring.h"
#include "../../arm64/include/virtio_adapter.h"
#include "virtio-blk.h"

/* QEMU virt's VirtIO-MMIO modern transport.  AArch64 uses the EXACT
 * SeaBIOS VirtIO request header, status constants and split-ring layouts.
 * Handles v1 legacy and v2 modern MMIO on QEMU virt, but refuses IOMMUs.
 * No interrupts, guest physical address translation or write support. */
#define MMIO_MAGIC           0x000u
#define MMIO_VERSION         0x004u
#define MMIO_DEVICE_ID       0x008u
#define MMIO_VENDOR_ID       0x00cu
#define MMIO_DEVICE_FEATURES 0x010u
#define MMIO_DEV_FEAT_SEL    0x014u
#define MMIO_DRV_FEATURES    0x020u
#define MMIO_GUEST_PAGE_SIZE 0x028u
#define MMIO_DRV_FEAT_SEL    0x024u
#define MMIO_QUEUE_SEL       0x030u
#define MMIO_QUEUE_NUM_MAX   0x034u
#define MMIO_QUEUE_NUM       0x038u
#define MMIO_QUEUE_ALIGN     0x03cu
#define MMIO_QUEUE_PFN       0x040u
#define MMIO_QUEUE_READY     0x044u
#define MMIO_QUEUE_NOTIFY    0x050u
#define MMIO_INT_STATUS      0x060u
#define MMIO_INT_ACK         0x064u
#define MMIO_STATUS          0x070u
#define MMIO_QUEUE_DESC_LO   0x080u
#define MMIO_QUEUE_DESC_HI   0x084u
#define MMIO_QUEUE_DRV_LO    0x090u
#define MMIO_QUEUE_DRV_HI    0x094u
#define MMIO_QUEUE_DEV_LO    0x0a0u
#define MMIO_QUEUE_DEV_HI    0x0a4u
#define MMIO_CONFIG_GENERATION 0x0fcu
#define MMIO_BLK_CAPACITY_LO 0x100u
#define MMIO_BLK_CAPACITY_HI 0x104u
#define VIRTIO_MAGIC        0x74726976u
#define VIRTIO_BLK_ID       2u
#define VIRTQ_SIZE          8u
#define SECTOR_SIZE         512u
#define MAX_READ_SECTORS    16u
/* Bounded polling is necessary until PSCI timers and IRQ/GIC exist. */
#define WAIT_SPINS          5000000u
#define DEVICE_NEEDS_RESET  64u

/* Independent split rings per device, not one global VirtIO context.
 * The same upstream SeaBIOS ring implementation is used for every disk. */
struct arm64_block_device {
    uintptr_t base;
    uint64_t sectors;
    uint8_t failed;
    uint8_t version;
    struct vring_virtqueue queue __attribute__((aligned(4096)));
    struct vp_device transport;
    struct virtio_blk_outhdr request __attribute__((aligned(16)));
    uint8_t status;
    uint8_t transfer[SECTOR_SIZE * MAX_READ_SECTORS]
        __attribute__((aligned(16)));
};
static struct arm64_block_device BlockDevs[ARM64_MAX_BLOCK_DRIVES]
    __attribute__((aligned(4096)));
static uint32_t BlockCount;


void arm64_virtio_barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t rd(uintptr_t base, unsigned off)
{
    return *(volatile const uint32_t *)(base + off);
}
static void wr(uintptr_t base, unsigned off, uint32_t value)
{
    *(volatile uint32_t *)(base + off) = value;
}
static void wraddr(uintptr_t base, unsigned lowoff, uintptr_t value)
{
    wr(base, lowoff, (uint32_t)value);
    wr(base, lowoff + 4u, (uint32_t)((uint64_t)value >> 32));
}

/* Reset completion precedes reuse of queue storage or OS ownership. */
static int block_reset(uintptr_t base)
{
    wr(base, MMIO_STATUS, 0u);
    arm64_virtio_barrier();
    uint64_t deadline = arm64_timer_deadline_ms(2000);
    uint32_t spins = WAIT_SPINS;
    while (rd(base, MMIO_STATUS)) {
        if (!spins-- || arm64_timer_expired(deadline))
            return -1;
        __asm__ volatile("yield" ::: "memory");
    }
    arm64_virtio_barrier();
    return 0;
}

/* A modern capacity is atomic only across matching config generations.
 * Legacy MMIO has no generation register, so require two matching reads. */
static int block_capacity(uintptr_t base, uint32_t version, uint64_t *capacity)
{
    for (uint32_t retry = 0; retry < 32u; ++retry) {
        uint32_t generation = version == 2u ? rd(base, MMIO_CONFIG_GENERATION) : 0;
        uint64_t first = rd(base, MMIO_BLK_CAPACITY_LO);
        first |= (uint64_t)rd(base, MMIO_BLK_CAPACITY_HI) << 32;
        arm64_virtio_barrier();
        if (version == 2u) {
            if (generation != rd(base, MMIO_CONFIG_GENERATION))
                continue;
        } else {
            uint64_t second = rd(base, MMIO_BLK_CAPACITY_LO);
            second |= (uint64_t)rd(base, MMIO_BLK_CAPACITY_HI) << 32;
            if (first != second)
                continue;
        }
        *capacity = first;
        return first ? 0 : -1;
    }
    return -1;
}

void vp_notify(struct vp_device *vp, struct vring_virtqueue *vq)
{
    (void)vq;
    wr(vp->mmio, MMIO_QUEUE_NOTIFY, 0);
}
void arm64_virtio_panic(void)
{
    uart_puts("[virtio] internal ring error - halt\n");
    for (;;) __asm__ volatile("wfe");
}

/* A successful initialization means ONLY one read-only block queue exists. */
static int block_init(uintptr_t base)
{
    uint32_t version = rd(base, MMIO_VERSION);
    if (BlockCount >= ARM64_MAX_BLOCK_DRIVES ||
        (version != 1u && version != 2u) ||
        rd(base, MMIO_DEVICE_ID) != VIRTIO_BLK_ID)
        return -1;
    struct arm64_block_device *dev = &BlockDevs[BlockCount];
    struct vring_virtqueue *vq = &dev->queue;
    /* A failed negotiation must not leak ring indices into the next probe. */
    uint8_t *raw = (uint8_t *)vq;
    for (uint32_t i = 0; i < sizeof(*vq); ++i)
        raw[i] = 0;
    if (block_reset(base))
        return -1;
    wr(base, MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                          VIRTIO_CONFIG_S_DRIVER);
    if (version == 2u) {
        wr(base, MMIO_DEV_FEAT_SEL, 1u);
        uint32_t high = rd(base, MMIO_DEVICE_FEATURES);
        if (!(high & 1u) || (high & 2u))
            goto error;
        wr(base, MMIO_DRV_FEAT_SEL, 0u);
        wr(base, MMIO_DRV_FEATURES, 0u);
        wr(base, MMIO_DRV_FEAT_SEL, 1u);
        wr(base, MMIO_DRV_FEATURES, 1u);
        wr(base, MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                              VIRTIO_CONFIG_S_DRIVER |
                              VIRTIO_CONFIG_S_FEATURES_OK);
        if (!(rd(base, MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
            goto error;
    } else {
        /* The 0.9 legacy transport has only low 32-bit features. */
        wr(base, MMIO_DRV_FEATURES, 0u);
    }
    wr(base, MMIO_QUEUE_SEL, 0u);
    if (rd(base, MMIO_QUEUE_NUM_MAX) < VIRTQ_SIZE)
        goto error;
    wr(base, MMIO_QUEUE_NUM, VIRTQ_SIZE);
    vring_init(&vq->vring, VIRTQ_SIZE, vq->queue);
    vq->free_head = 0;
    vq->last_used_idx = 0;
    if (version == 2u) {
        if (rd(base, MMIO_QUEUE_READY))
            goto error;
        wraddr(base, MMIO_QUEUE_DESC_LO,
               (uintptr_t)vq->vring.desc);
        wraddr(base, MMIO_QUEUE_DRV_LO,
               (uintptr_t)vq->vring.avail);
        wraddr(base, MMIO_QUEUE_DEV_LO,
               (uintptr_t)vq->vring.used);
        arm64_virtio_barrier();
        wr(base, MMIO_QUEUE_READY, 1u);
        if (!rd(base, MMIO_QUEUE_READY))
            goto error;
    } else {
        uintptr_t addr = (uintptr_t)vq->vring.desc;
        if (addr > UINT32_MAX || (addr & 4095u))
            goto error;
        wr(base, MMIO_GUEST_PAGE_SIZE, 4096u);
        wr(base, MMIO_QUEUE_ALIGN, 4096u);
        arm64_virtio_barrier();
        wr(base, MMIO_QUEUE_PFN, (uint32_t)(addr >> 12));
        if (rd(base, MMIO_QUEUE_PFN) != (uint32_t)(addr >> 12))
            goto error;
    }
    /* Read the 512-byte-sector count from device config. */
    uint64_t sectors;
    if (block_capacity(base, version, &sectors))
        goto error;
    wr(base, MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                          VIRTIO_CONFIG_S_DRIVER |
                          (version == 2u ? VIRTIO_CONFIG_S_FEATURES_OK : 0u) |
                          VIRTIO_CONFIG_S_DRIVER_OK);
    if (!(rd(base, MMIO_STATUS) & VIRTIO_CONFIG_S_DRIVER_OK))
        goto error;
    dev->base = base;
    dev->sectors = sectors;
    dev->failed = 0;
    dev->version = version;
    dev->transport.mmio = base;
    ++BlockCount;
    return 0;
error:
    wr(base, MMIO_STATUS, rd(base, MMIO_STATUS) | VIRTIO_CONFIG_S_FAILED);
    return -1;
}

/* One in-flight request per disk, independent VirtIO split rings.
 * The original SeaBIOS virtio_blk_outhdr and vring implementation remain. */
int arm64_virtio_blk_read_many_transport_at(uint32_t index, uint64_t lba,
                                            void *out, uint32_t count)
{
    if (index >= BlockCount || !out || !count ||
        count > MAX_READ_SECTORS)
        return -1;
    struct arm64_block_device *dev = &BlockDevs[index];
    if (!dev->base || dev->failed ||
        lba >= dev->sectors || count > dev->sectors - lba)
        return -1;
    dev->request.type = VIRTIO_BLK_T_IN;
    dev->request.ioprio = 0;
    dev->request.sector = lba;
    dev->status = 0xffu;
    /* Legacy devices may not report a usable used.len (VirtIO 5.2.6.3).
     * Clear the bounce buffer so a short buggy reply cannot reuse old data. */
    if (dev->version == 1u)
        for (uint32_t i = 0; i < SECTOR_SIZE * count; ++i)
            dev->transfer[i] = 0;
    struct vring_list sg[] = {
        { (char *)&dev->request, sizeof(dev->request) },
        { (char *)dev->transfer, SECTOR_SIZE * count },
        { (char *)&dev->status, 1 },
    };
    uint16_t head = dev->queue.free_head;
    vring_add_buf(&dev->queue, sg, 1, 2, 0, 0);
    vring_kick(&dev->transport, &dev->queue, 1);
    uint32_t spins = WAIT_SPINS;
    uint64_t deadline = arm64_timer_deadline_ms(2000);
    while (!vring_more_used(&dev->queue) && spins--) {
        if (arm64_timer_expired(deadline) ||
            (rd(dev->base, MMIO_STATUS) & DEVICE_NEEDS_RESET)) break;
        __asm__ volatile("yield" ::: "memory");
    }
    arm64_virtio_barrier();
    struct vring_virtqueue *q = &dev->queue;
    if (!vring_more_used(q) ||
        (uint16_t)(q->vring.used->idx - q->last_used_idx) != 1u ||
        q->vring.used->ring[q->last_used_idx % VIRTQ_SIZE].id != head ||
        (dev->version == 2u &&
         q->vring.used->ring[q->last_used_idx % VIRTQ_SIZE].len
            != SECTOR_SIZE * count + 1) || dev->status != VIRTIO_BLK_S_OK ||
        (rd(dev->base, MMIO_STATUS) & DEVICE_NEEDS_RESET)) {
        block_reset(dev->base);
        dev->failed = 1;
        uart_puts("[virtio-blk] controller failed; isolated disk ");
        uart_dec(index); uart_newline();
        return -2;
    }
    vring_get_buf(q, NULL);
    uint32_t irq = rd(dev->base, MMIO_INT_STATUS);
    if (irq) wr(dev->base, MMIO_INT_ACK, irq);
    uint8_t *dst = out;
    for (uint32_t i = 0; i < SECTOR_SIZE * count; i++)
        dst[i] = dev->transfer[i];
    return 0;
}

int arm64_virtio_blk_read_many_transport(uint64_t lba, void *out,
                                        uint32_t count)
{
    return arm64_virtio_blk_read_many_transport_at(0, lba, out, count);
}

uint32_t arm64_virtio_blk_count(void)
{
    return BlockCount;
}

uintptr_t arm64_virtio_blk_mmio_at(uint32_t index)
{
    return index < BlockCount && !BlockDevs[index].failed
        ? BlockDevs[index].base : 0;
}

uint64_t arm64_virtio_blk_capacity_at(uint32_t index)
{
    return index < BlockCount && BlockDevs[index].base && !BlockDevs[index].failed
        ? BlockDevs[index].sectors : 0;
}

uintptr_t arm64_virtio_blk_mmio(void)
{
    return arm64_virtio_blk_mmio_at(0);
}

uint64_t arm64_virtio_blk_capacity(void)
{
    return arm64_virtio_blk_capacity_at(0);
}

int arm64_virtio_quiesce(void)
{
    for (uint32_t index = 0; index < BlockCount; ++index) {
        /* Keep the address even after failure so cleanup can verify reset. */
        if (block_reset(BlockDevs[index].base)) {
            uart_puts("[virtio-blk] cannot stop DMA; refusing handoff\n");
            return -1;
        }
        BlockDevs[index].failed = 1;
    }
    if (BlockCount)
        uart_puts("[virtio-blk] all queues reset before native handoff\n");
    return 0;
}

void arm64_virtio_discover(const struct fdt_info *info)
{
    uart_puts("[virtio] MMIO transports in DTB: ");
    uart_dec(info->virtio_count);
    uart_newline();
    for (uint32_t i = 0; i < info->virtio_count; ++i) {
        const struct fdt_region *region = &info->virtio_mmio[i];
        if (region->base & 3u || region->size < 0x200 ||
            region->base > UINTPTR_MAX - 0x200u)
            continue;
        uintptr_t base = (uintptr_t)region->base;
        if (rd(base, MMIO_MAGIC) != VIRTIO_MAGIC)
            continue;
        uint32_t version = rd(base, MMIO_VERSION);
        uint32_t device = rd(base, MMIO_DEVICE_ID);
        uint32_t vendor = rd(base, MMIO_VENDOR_ID);
        if (!device)
            continue;
        uart_puts("[virtio] slot: ");
        uart_hex(base);
        uart_puts(" version: ");
        uart_dec(version);
        uart_puts(" device: ");
        uart_dec(device);
        uart_puts(" vendor: ");
        uart_hex(vendor);
        uart_newline();
        if (device == VIRTIO_BLK_ID &&
            BlockCount < ARM64_MAX_BLOCK_DRIVES) {
            if (!block_init(base)) {
                uart_puts("[virtio-blk] registered disk ");
                uart_dec(BlockCount - 1);
                uart_puts(", sectors: ");
                uart_hex(BlockDevs[BlockCount - 1].sectors);
                uart_newline();
                uart_puts("[virtio-blk] ready, sectors: ");
                uart_hex(BlockDevs[BlockCount - 1].sectors);
                uart_newline();
            } else {
                uart_puts("[virtio-blk] unsupported or initialization failed\n");
            }
        }
    }
}

#else /* Original SeaBIOS x86 VirtIO MMIO implementation */
#include "config.h" // CONFIG_DEBUG_LEVEL
#include "malloc.h" // free
#include "output.h" // dprintf
#include "stacks.h" // run_thread
#include "string.h" // memset
#include "util.h" // acpi_dsdt_*
#include "virtio-pci.h"
#include "virtio-blk.h"
#include "virtio-scsi.h"
#include "virtio-ring.h"
#include "virtio-mmio.h"

void virtio_mmio_setup_acpi(void)
{
    static const char *virtio_hid = "LNRO0005";
    struct acpi_device *dev;
    u64 mem, irq, unused;

    for (dev = acpi_dsdt_find_string(NULL, virtio_hid);
         dev != NULL;
         dev = acpi_dsdt_find_string(dev, virtio_hid)) {
        if (acpi_dsdt_find_mem(dev, &mem, &unused) < 0)
            continue;
        if (acpi_dsdt_find_irq(dev, &irq) < 0)
            continue;
        dprintf(1, "ACPI: virtio-mmio device %s at 0x%llx, irq %lld\n",
                acpi_dsdt_name(dev), mem, irq);
        virtio_mmio_setup_one(mem);
    }
}

void virtio_mmio_setup_one(u64 addr)
{
    static const char *names[] = {
        [  1 ] = "net",
        [  2 ] = "blk",
        [  3 ] = "console",
        [  4 ] = "rng",
        [  8 ] = "scsi",
        [  9 ] = "9p",
        [ 16 ] = "gpu",
        [ 19 ] = "vsock",
        [ 18 ] = "input",
        [ 26 ] = "fs",
    };
    const char *name;
    u32 magic, version, devid;
    void *mmio;

    if (addr >= 0x100000000) {
        dprintf(1, "virtio-mmio: %llx: above 4G\n", addr);
        return;
    }

    mmio = (void*)(u32)(addr);
    magic = readl(mmio);
    if (magic != 0x74726976) {
        dprintf(1, "virtio-mmio: %llx: magic mismatch\n", addr);
        return;
    }
    version = readl(mmio+4);
    if (version != 1 /* legacy */ &&
        version != 2 /* 1.0 */) {
        dprintf(1, "virtio-mmio: %llx: unknown version %d\n", addr, version);
        return;
    }
    devid = readl(mmio+8);

    name = (devid < ARRAY_SIZE(names) && names[devid] != NULL)
        ? names[devid] : "unknown";
    dprintf(1, "virtio-mmio: %llx: device id %x (%s%s)\n",
            addr, devid, name, version == 1 ? ", legacy" : "");

    switch (devid) {
    case 2: /* blk */
        run_thread(init_virtio_blk_mmio, mmio);
        break;
    case 8: /* scsi */
        run_thread(init_virtio_scsi_mmio, mmio);
        break;
    default:
        break;
    }
}

void vp_init_mmio(struct vp_device *vp, void *mmio)
{
    memset(vp, 0, sizeof(*vp));
    vp->use_mmio = 1;
    vp->common.mode = VP_ACCESS_MMIO;
    vp->common.memaddr = mmio;
    vp->device.mode = VP_ACCESS_MMIO;
    vp->device.memaddr = mmio + 0x100;
    vp_reset(vp);
    vp_set_status(vp, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                  VIRTIO_CONFIG_S_DRIVER);
}

#endif /* CONFIG_AARCH64 */
