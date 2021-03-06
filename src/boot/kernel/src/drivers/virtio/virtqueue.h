#ifndef KERNEL_VIRTQUEUE_H
#define KERNEL_VIRTQUEUE_H

#include "virtio.h"

#define VIRTIO_RING_SIZE (1 << 7)

enum {
    VIRTIO_DESCRIPTOR_FLAG_NEXT = 1,
    VIRTIO_DESCRIPTOR_FLAG_WRITE_ONLY = 2,
    VIRTIO_DESCRIPTOR_FLAG_INDIRECT = 4,
};

typedef struct __attribute__((__packed__, aligned(1))) {
    volatile void* addr;
    unsigned int length;
    unsigned short flags;
    unsigned short next;
} virtio_descriptor_t;

typedef struct __attribute__((__packed__, aligned(1))) {
    unsigned short flags;
    unsigned short idx;
    unsigned short ring[VIRTIO_RING_SIZE];
    unsigned short event;
    char padding[2];
} virtio_available_t;

typedef struct __attribute__((__packed__, aligned(1))) {
    unsigned short flags;
    unsigned short idx;
    struct __attribute__((__packed__, aligned(1))) {
        unsigned int id;
        unsigned int length;
    } ring[VIRTIO_RING_SIZE];
    unsigned short event;
} virtio_used_t;

typedef struct __attribute__((__packed__, aligned(1))) {
    volatile unsigned int num;
    volatile unsigned int last_seen_used;
    volatile virtio_descriptor_t* desc;
    volatile virtio_available_t* available;
    volatile virtio_used_t* used;
    char padding[8];
} virtio_queue_t;

// virtqueue_add_to_device(volatile virtio_mmio_t* mmio, unsigned int) -> volatile virtio_queue_t*
// Adds a virtqueue to a device.
volatile virtio_queue_t* virtqueue_add_to_device(volatile virtio_mmio_t* mmio, unsigned int queue_sel);

// virtqueue_push_descriptor(volatile virtio_queue_t*, unsigned short*) -> volatile virtio_descriptor_t*
// Pushes a descriptor to a queue.
volatile virtio_descriptor_t* virtqueue_push_descriptor(volatile virtio_queue_t* queue, unsigned short* desc_index);

// virtqueue_push_available(volatile virtio_queue_t*, unsigned short) -> void
// Pushes an available descriptor to a queue.
void virtqueue_push_available(volatile virtio_queue_t* queue, unsigned short desc);

// virtqueue_pop_used(volatile virtio_queue_t*) -> volatile virtio_descriptor_t*
// Pops a used descriptor from a queue.
volatile virtio_descriptor_t* virtqueue_pop_used(volatile virtio_queue_t* queue);

#endif /* KERNEL_VIRTQUEUE_H */

