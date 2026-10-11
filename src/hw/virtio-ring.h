#ifndef _VIRTIO_RING_H
#define _VIRTIO_RING_H

#include "types.h" // u64
#if CONFIG_AARCH64
#include <stdint.h>
#define PAGE_SIZE 4096
#else
#include "memmap.h" // PAGE_SIZE
#endif

/* Architecture-neutral definitions shared with the AArch64 port. */
#include "virtio-ring-common.h"

#define MAX_QUEUE_NUM (256)

struct vring {
   unsigned int num;
   struct vring_desc *desc;
   struct vring_avail *avail;
   struct vring_used *used;
};

#define vring_size(num) \
    (ALIGN(sizeof(struct vring_desc) * num + sizeof(struct vring_avail) \
           + sizeof(u16) * num, PAGE_SIZE)                              \
     + sizeof(struct vring_used) + sizeof(struct vring_used_elem) * num)

typedef unsigned char virtio_queue_t[vring_size(MAX_QUEUE_NUM)];

struct vring_virtqueue {
   virtio_queue_t queue;
   struct vring vring;
   u16 free_head;
   u16 last_used_idx;
   u16 vdata[MAX_QUEUE_NUM];
   /* PCI */
   int queue_index;
   int queue_notify_off;
};

struct vring_list {
  char *addr;
  unsigned int length;
};

static inline void
vring_init(struct vring *vr, unsigned int num, unsigned char *queue)
{
   ASSERT32FLAT();
   vr->num = num;

   /* physical address of desc must be page aligned */
#if CONFIG_AARCH64
   vr->desc = (void*)ALIGN((uintptr_t)queue, PAGE_SIZE);
#else
   vr->desc = (void*)ALIGN((u32)queue, PAGE_SIZE);
#endif

   vr->avail = (struct vring_avail *)&vr->desc[num];
   /* disable interrupts */
   vr->avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

   /* physical address of used must be page aligned */
#if CONFIG_AARCH64
   vr->used = (void*)ALIGN((uintptr_t)&vr->avail->ring[num], PAGE_SIZE);
#else
   vr->used = (void*)ALIGN((u32)&vr->avail->ring[num], PAGE_SIZE);
#endif

#if CONFIG_AARCH64
   unsigned int i;
#else
   int i; /* Preserve original SeaBIOS x86 loop type. */
#endif
   for (i = 0; i < num - 1; i++)
       vr->desc[i].next = i + 1;
   vr->desc[i].next = 0;
}

struct vp_device;
int vring_more_used(struct vring_virtqueue *vq);
void vring_detach(struct vring_virtqueue *vq, unsigned int head);
int vring_get_buf(struct vring_virtqueue *vq, unsigned int *len);
void vring_add_buf(struct vring_virtqueue *vq, struct vring_list list[],
                   unsigned int out, unsigned int in,
                   int index, int num_added);
void vring_kick(struct vp_device *vp, struct vring_virtqueue *vq, int num_added);

#endif /* _VIRTIO_RING_H_ */
