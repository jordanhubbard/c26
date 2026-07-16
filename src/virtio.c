#include "c26_virtio.h"

#define VIRTIO_MMIO_FIRST 0x10001000UL
#define VIRTIO_MMIO_LAST 0x10008000UL
#define VIRTIO_MMIO_STEP 0x1000UL

#define MMIO_MAGIC_VALUE 0x000
#define MMIO_VERSION 0x004
#define MMIO_DEVICE_ID 0x008
#define MMIO_DEVICE_FEATURES 0x010
#define MMIO_DEVICE_FEATURES_SEL 0x014
#define MMIO_DRIVER_FEATURES 0x020
#define MMIO_DRIVER_FEATURES_SEL 0x024
#define MMIO_QUEUE_SEL 0x030
#define MMIO_QUEUE_NUM_MAX 0x034
#define MMIO_QUEUE_NUM 0x038
#define MMIO_QUEUE_READY 0x044
#define MMIO_QUEUE_NOTIFY 0x050
#define MMIO_INTERRUPT_STATUS 0x060
#define MMIO_INTERRUPT_ACK 0x064
#define MMIO_STATUS 0x070
#define MMIO_QUEUE_DESC_LOW 0x080
#define MMIO_QUEUE_DESC_HIGH 0x084
#define MMIO_QUEUE_AVAIL_LOW 0x090
#define MMIO_QUEUE_AVAIL_HIGH 0x094
#define MMIO_QUEUE_USED_LOW 0x0a0
#define MMIO_QUEUE_USED_HIGH 0x0a4
#define MMIO_CONFIG 0x100

#define VIRTIO_MAGIC 0x74726976U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER 2U
#define VIRTIO_STATUS_DRIVER_OK 4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTIO_STATUS_FAILED 128U
#define VIRTIO_F_VERSION_1_HIGH 1U

static uint32_t mmio_read(uintptr_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void mmio_write(uintptr_t base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
}

void c26_memory_barrier(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

int c26_virtio_find(uint32_t device_id, unsigned int instance,
                    c26_virtio_device_t *device)
{
    for (uintptr_t base = VIRTIO_MMIO_FIRST; base <= VIRTIO_MMIO_LAST;
         base += VIRTIO_MMIO_STEP) {
        if (mmio_read(base, MMIO_MAGIC_VALUE) != VIRTIO_MAGIC ||
            mmio_read(base, MMIO_VERSION) != 2 ||
            mmio_read(base, MMIO_DEVICE_ID) != device_id) {
            continue;
        }
        if (instance != 0) {
            instance--;
            continue;
        }
        device->base = base;
        device->device_id = device_id;
        return 1;
    }
    return 0;
}

int c26_virtio_begin(c26_virtio_device_t *device, uint32_t wanted_low,
                     uint32_t wanted_high)
{
    uintptr_t base = device->base;
    mmio_write(base, MMIO_STATUS, 0);
    c26_memory_barrier();
    mmio_write(base, MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write(base, MMIO_STATUS,
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write(base, MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t offered_low = mmio_read(base, MMIO_DEVICE_FEATURES);
    mmio_write(base, MMIO_DEVICE_FEATURES_SEL, 1);
    uint32_t offered_high = mmio_read(base, MMIO_DEVICE_FEATURES);
    if ((offered_high & VIRTIO_F_VERSION_1_HIGH) == 0) {
        mmio_write(base, MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return 0;
    }

    uint32_t accepted_low = offered_low & wanted_low;
    uint32_t accepted_high = (offered_high & wanted_high) |
                             VIRTIO_F_VERSION_1_HIGH;
    device->features_low = accepted_low;
    device->features_high = accepted_high;
    mmio_write(base, MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write(base, MMIO_DRIVER_FEATURES, accepted_low);
    mmio_write(base, MMIO_DRIVER_FEATURES_SEL, 1);
    mmio_write(base, MMIO_DRIVER_FEATURES, accepted_high);

    uint32_t status = mmio_read(base, MMIO_STATUS) |
                      VIRTIO_STATUS_FEATURES_OK;
    mmio_write(base, MMIO_STATUS, status);
    if ((mmio_read(base, MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        mmio_write(base, MMIO_STATUS, status | VIRTIO_STATUS_FAILED);
        return 0;
    }
    return 1;
}

static void write_address(uintptr_t base, uint32_t low_offset,
                          uint32_t high_offset, uintptr_t address)
{
    mmio_write(base, low_offset, (uint32_t)address);
    mmio_write(base, high_offset, (uint32_t)((uint64_t)address >> 32));
}

int c26_virtio_queue_setup(c26_virtio_device_t *device, uint16_t number,
                           c26_virtq_t *queue,
                           c26_virtq_desc_t *descriptors,
                           c26_virtq_avail_t *available,
                           c26_virtq_used_t *used,
                           uint16_t requested_size)
{
    uintptr_t base = device->base;
    mmio_write(base, MMIO_QUEUE_SEL, number);
    if (mmio_read(base, MMIO_QUEUE_READY) != 0) {
        return 0;
    }
    uint32_t maximum = mmio_read(base, MMIO_QUEUE_NUM_MAX);
    if (maximum == 0) {
        return 0;
    }
    uint16_t size = requested_size;
    if (size > C26_VIRTQ_SIZE) {
        size = C26_VIRTQ_SIZE;
    }
    if (size > maximum) {
        size = (uint16_t)maximum;
    }

    for (unsigned int i = 0; i < C26_VIRTQ_SIZE; i++) {
        descriptors[i] = (c26_virtq_desc_t){0, 0, 0, 0};
        available->ring[i] = 0;
        used->ring[i] = (c26_virtq_used_elem_t){0, 0};
    }
    available->flags = 0;
    available->index = 0;
    available->used_event = 0;
    used->flags = 0;
    used->index = 0;
    used->avail_event = 0;

    mmio_write(base, MMIO_QUEUE_NUM, size);
    write_address(base, MMIO_QUEUE_DESC_LOW, MMIO_QUEUE_DESC_HIGH,
                  (uintptr_t)descriptors);
    write_address(base, MMIO_QUEUE_AVAIL_LOW, MMIO_QUEUE_AVAIL_HIGH,
                  (uintptr_t)available);
    write_address(base, MMIO_QUEUE_USED_LOW, MMIO_QUEUE_USED_HIGH,
                  (uintptr_t)used);
    c26_memory_barrier();
    mmio_write(base, MMIO_QUEUE_READY, 1);

    queue->device = device;
    queue->number = number;
    queue->size = size;
    queue->last_used = 0;
    queue->descriptors = descriptors;
    queue->available = available;
    queue->used = used;
    return 1;
}

int c26_virtio_finish(c26_virtio_device_t *device)
{
    uint32_t status = mmio_read(device->base, MMIO_STATUS);
    mmio_write(device->base, MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);
    c26_memory_barrier();
    return (mmio_read(device->base, MMIO_STATUS) & VIRTIO_STATUS_DRIVER_OK) != 0;
}

void c26_virtq_submit(c26_virtq_t *queue, uint16_t head)
{
    uint16_t index = queue->available->index;
    queue->available->ring[index % queue->size] = head;
    c26_memory_barrier();
    queue->available->index = (uint16_t)(index + 1);
    c26_memory_barrier();
    mmio_write(queue->device->base, MMIO_QUEUE_NOTIFY, queue->number);
}

int c26_virtq_pop(c26_virtq_t *queue, uint32_t *id, uint32_t *length)
{
    if (queue->last_used == queue->used->index) {
        return 0;
    }
    c26_memory_barrier();
    c26_virtq_used_elem_t element =
        queue->used->ring[queue->last_used % queue->size];
    queue->last_used++;
    if (id != 0) {
        *id = element.id;
    }
    if (length != 0) {
        *length = element.length;
    }
    return 1;
}

void c26_virtio_ack_interrupt(c26_virtio_device_t *device)
{
    uint32_t status = mmio_read(device->base, MMIO_INTERRUPT_STATUS);
    if (status != 0) {
        mmio_write(device->base, MMIO_INTERRUPT_ACK, status);
    }
}

uint32_t c26_virtio_config_read32(c26_virtio_device_t *device,
                                  uint32_t offset)
{
    return mmio_read(device->base, MMIO_CONFIG + offset);
}
