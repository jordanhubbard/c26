#include "c26.h"
#include "c26_block.h"
#include "c26_virtio.h"

#define VIRTIO_DEVICE_BLOCK 2U
#define VIRTIO_BLK_F_FLUSH (1U << 9)
#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U
#define VIRTIO_BLK_T_FLUSH 4U
#define VIRTIO_BLK_S_OK 0U

/* Completion wait: BLOCK_WAIT_POLL quick used-ring polls catch the common
   instant completion; otherwise WFI sleeps the core until the disk IRQ or the
   next timer tick, up to BLOCK_WAIT_SLEEPS times (~seconds) before giving up. */
#define BLOCK_WAIT_POLL 4096
#define BLOCK_WAIT_SLEEPS 1024U

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} block_request_t;

static c26_virtio_device_t block_device;
static c26_virtq_t block_queue;
static c26_virtq_desc_t block_descriptors[C26_VIRTQ_SIZE]
    __attribute__((aligned(4096)));
static c26_virtq_avail_t block_available __attribute__((aligned(4096)));
static c26_virtq_used_t block_used __attribute__((aligned(4096)));
static block_request_t request;
static uint8_t request_status;
static uint64_t sector_count;
static int online;

static int submit(uint32_t type, uint64_t sector, void *buffer,
                  uint32_t length, int device_writes)
{
    if (!online || (type != VIRTIO_BLK_T_FLUSH && sector >= sector_count)) {
        return 0;
    }
    request = (block_request_t){type, 0, sector};
    request_status = 0xff;
    block_descriptors[0] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&request, sizeof(request), C26_VIRTQ_DESC_NEXT, 1};

    uint16_t status_descriptor = 1;
    if (length != 0) {
        uint16_t flags = C26_VIRTQ_DESC_NEXT;
        if (device_writes) {
            flags |= C26_VIRTQ_DESC_WRITE;
        }
        block_descriptors[1] = (c26_virtq_desc_t){
            (uint64_t)(uintptr_t)buffer, length, flags, 2};
        status_descriptor = 2;
    }
    block_descriptors[status_descriptor] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&request_status, sizeof(request_status),
        C26_VIRTQ_DESC_WRITE, 0};
    c26_virtq_submit(&block_queue, 0);

    /* Wait for the device to post completion in the used ring. Rather than
       burn the core (and, under SMP, hold the big kernel lock) in a spin, we
       poll briefly to catch the common instant completion, then WFI to sleep
       until the virtio-blk interrupt — or, as a fallback, the next timer tick —
       wakes the hart, and re-check. `sleeps` bounds the wait so a dead device
       can't hang the machine. */
    uint32_t id;
    for (unsigned int sleeps = 0; sleeps < BLOCK_WAIT_SLEEPS; sleeps++) {
        for (int quick = 0; quick < BLOCK_WAIT_POLL; quick++) {
            if (c26_virtq_pop(&block_queue, &id, 0)) {
                c26_virtio_ack_interrupt(&block_device);
                return id == 0 && request_status == VIRTIO_BLK_S_OK;
            }
        }
        __asm__ volatile("wfi");
    }
    return 0;
}

int c26_block_init(void)
{
    online = 0;
    if (!c26_virtio_find(VIRTIO_DEVICE_BLOCK, 0, &block_device) ||
        !c26_virtio_begin(&block_device, VIRTIO_BLK_F_FLUSH, 0) ||
        !c26_virtio_queue_setup(&block_device, 0, &block_queue,
            block_descriptors, &block_available, &block_used, 8) ||
        !c26_virtio_finish(&block_device)) {
        c26_puts("VIRTIO BLOCK: not present\n");
        return 0;
    }
    sector_count = c26_virtio_config_read32(&block_device, 0);
    sector_count |= (uint64_t)c26_virtio_config_read32(&block_device, 4) << 32;
    online = sector_count != 0;
    c26_puts("VIRTIO BLOCK: ");
    if (online) {
        c26_put_uint(sector_count);
        c26_puts(" sectors online\n");
    } else {
        c26_puts("invalid zero capacity\n");
    }
    return online;
}

int c26_block_online(void)
{
    return online;
}

uint64_t c26_block_sector_count(void)
{
    return sector_count;
}

int c26_block_read(uint64_t sector, void *buffer)
{
    return buffer != 0 && submit(VIRTIO_BLK_T_IN, sector, buffer,
                                 C26_BLOCK_SECTOR_SIZE, 1);
}

int c26_block_write(uint64_t sector, const void *buffer)
{
    return buffer != 0 && submit(VIRTIO_BLK_T_OUT, sector, (void *)buffer,
                                 C26_BLOCK_SECTOR_SIZE, 0);
}

int c26_block_flush(void)
{
    if ((block_device.features_low & VIRTIO_BLK_F_FLUSH) == 0) {
        return online;
    }
    return submit(VIRTIO_BLK_T_FLUSH, 0, 0, 0, 0);
}
