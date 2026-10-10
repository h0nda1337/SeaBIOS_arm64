/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef ARM64_VIRTIO_ADAPTER_H
#define ARM64_VIRTIO_ADAPTER_H
#include <stdint.h>
struct vp_device { uintptr_t mmio; };
struct vring_virtqueue;
void vp_notify(struct vp_device *vp, struct vring_virtqueue *vq);
void arm64_virtio_barrier(void);
void arm64_virtio_panic(void) __attribute__((noreturn));
#endif
