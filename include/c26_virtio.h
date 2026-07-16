#ifndef C26_VIRTIO_H
#define C26_VIRTIO_H

#include <stddef.h>
#include <stdint.h>

#define C26_VIRTQ_SIZE 32U

typedef struct {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} c26_virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[C26_VIRTQ_SIZE];
    uint16_t used_event;
} c26_virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t length;
} c26_virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t index;
    c26_virtq_used_elem_t ring[C26_VIRTQ_SIZE];
    uint16_t avail_event;
} c26_virtq_used_t;

typedef struct {
    uintptr_t base;
    uint32_t device_id;
} c26_virtio_device_t;

typedef struct {
    c26_virtio_device_t *device;
    uint16_t number;
    uint16_t size;
    uint16_t last_used;
    c26_virtq_desc_t *descriptors;
    c26_virtq_avail_t *available;
    volatile c26_virtq_used_t *used;
} c26_virtq_t;

enum {
    C26_VIRTQ_DESC_NEXT = 1,
    C26_VIRTQ_DESC_WRITE = 2,
};

int c26_virtio_find(uint32_t device_id, unsigned int instance,
                    c26_virtio_device_t *device);
int c26_virtio_begin(c26_virtio_device_t *device, uint32_t wanted_low,
                     uint32_t wanted_high);
int c26_virtio_queue_setup(c26_virtio_device_t *device, uint16_t number,
                           c26_virtq_t *queue,
                           c26_virtq_desc_t *descriptors,
                           c26_virtq_avail_t *available,
                           c26_virtq_used_t *used,
                           uint16_t requested_size);
int c26_virtio_finish(c26_virtio_device_t *device);
void c26_virtq_submit(c26_virtq_t *queue, uint16_t head);
int c26_virtq_pop(c26_virtq_t *queue, uint32_t *id, uint32_t *length);
void c26_virtio_ack_interrupt(c26_virtio_device_t *device);
uint32_t c26_virtio_config_read32(c26_virtio_device_t *device,
                                  uint32_t offset);
void c26_memory_barrier(void);

#endif
