/* Shared VirtIO split-ring structures for SeaBIOS x86 and AArch64.
 * Copyright the SeaBIOS contributors; LGPLv3 (see COPYING.LESSER).
 * Types and feature definitions originate in virtio-ring.h. */
#ifndef _VIRTIO_RING_COMMON_H
#define _VIRTIO_RING_COMMON_H
#include "types.h"

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER      2
#define VIRTIO_CONFIG_S_DRIVER_OK   4
#define VIRTIO_CONFIG_S_FEATURES_OK 8
#define VIRTIO_CONFIG_S_FAILED      0x80
#define VIRTIO_F_VERSION_1          32
#define VIRTIO_F_IOMMU_PLATFORM     33
#define VRING_DESC_F_NEXT           1
#define VRING_DESC_F_WRITE          2
#define VRING_AVAIL_F_NO_INTERRUPT  1
#define VRING_USED_F_NO_NOTIFY      1

struct vring_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};
struct vring_avail {
    u16 flags;
    u16 idx;
    u16 ring[0];
};
struct vring_used_elem {
    u32 id;
    u32 len;
};
struct vring_used {
    u16 flags;
    u16 idx;
    struct vring_used_elem ring[];
};
#endif
