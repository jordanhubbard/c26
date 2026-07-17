#include "c26.h"
#include "c26_graphics.h"
#include "c26_virtio.h"

#define VIRTIO_DEVICE_GPU 16U
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101U
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104U
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100U
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2U
#define GPU_RESOURCE_ID 1U

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t context_id;
    uint32_t padding;
} gpu_header_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} gpu_rect_t;

typedef struct {
    gpu_header_t header;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} gpu_create_2d_t;

typedef struct {
    gpu_header_t header;
    uint32_t resource_id;
    uint32_t entries;
    uint64_t address;
    uint32_t length;
    uint32_t padding;
} gpu_attach_backing_t;

typedef struct {
    gpu_header_t header;
    gpu_rect_t rectangle;
    uint32_t scanout_id;
    uint32_t resource_id;
} gpu_set_scanout_t;

typedef struct {
    gpu_header_t header;
    gpu_rect_t rectangle;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} gpu_transfer_t;

typedef struct {
    gpu_header_t header;
    gpu_rect_t rectangle;
    uint32_t resource_id;
    uint32_t padding;
} gpu_flush_t;

static uint32_t pixels[C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT]
    __attribute__((aligned(4096)));
static c26_virtio_device_t gpu;
static c26_virtq_t gpu_queue;
static c26_virtq_desc_t gpu_descriptors[C26_VIRTQ_SIZE]
    __attribute__((aligned(4096)));
static c26_virtq_avail_t gpu_available __attribute__((aligned(4096)));
static c26_virtq_used_t gpu_used __attribute__((aligned(4096)));
static gpu_header_t gpu_response __attribute__((aligned(16)));
static int gpu_online;

static int gpu_command(void *request, uint32_t request_length)
{
    gpu_response = (gpu_header_t){0, 0, 0, 0, 0};
    gpu_descriptors[0] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)request, request_length, C26_VIRTQ_DESC_NEXT, 1};
    gpu_descriptors[1] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)&gpu_response, sizeof(gpu_response),
        C26_VIRTQ_DESC_WRITE, 0};
    c26_virtq_submit(&gpu_queue, 0);

    uint32_t id;
    for (uint32_t spin = 0; spin < 50000000U; spin++) {
        if (c26_virtq_pop(&gpu_queue, &id, 0)) {
            c26_virtio_ack_interrupt(&gpu);
            return id == 0 && gpu_response.type == VIRTIO_GPU_RESP_OK_NODATA;
        }
    }
    return 0;
}

static int gpu_initialize(void)
{
    if (!c26_virtio_find(VIRTIO_DEVICE_GPU, 0, &gpu) ||
        !c26_virtio_begin(&gpu, 0, 0) ||
        !c26_virtio_queue_setup(&gpu, 0, &gpu_queue, gpu_descriptors,
                                &gpu_available, &gpu_used, C26_VIRTQ_SIZE) ||
        !c26_virtio_finish(&gpu)) {
        return 0;
    }

    gpu_create_2d_t create = {
        {VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, 0, 0, 0, 0},
        GPU_RESOURCE_ID,
        VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
        C26_SCREEN_WIDTH,
        C26_SCREEN_HEIGHT,
    };
    if (!gpu_command(&create, sizeof(create))) {
        return 0;
    }

    gpu_attach_backing_t attach = {
        {VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, 0, 0, 0, 0},
        GPU_RESOURCE_ID,
        1,
        (uint64_t)(uintptr_t)pixels,
        sizeof(pixels),
        0,
    };
    if (!gpu_command(&attach, sizeof(attach))) {
        return 0;
    }

    gpu_set_scanout_t scanout = {
        {VIRTIO_GPU_CMD_SET_SCANOUT, 0, 0, 0, 0},
        {0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT},
        0,
        GPU_RESOURCE_ID,
    };
    return gpu_command(&scanout, sizeof(scanout));
}

int c26_framebuffer_init(void)
{
    for (unsigned int i = 0; i < C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT; i++) {
        pixels[i] = 0;
    }
    gpu_online = gpu_initialize();
    c26_puts("FRAMEBUFFER: ");
    c26_puts(c26_framebuffer_backend());
    c26_puts(" 640x480x32\n");
    return gpu_online;
}

uint32_t *c26_framebuffer_pixels(void)
{
    return pixels;
}

unsigned int c26_framebuffer_width(void)
{
    return C26_SCREEN_WIDTH;
}

unsigned int c26_framebuffer_height(void)
{
    return C26_SCREEN_HEIGHT;
}

const char *c26_framebuffer_backend(void)
{
    return gpu_online ? "virtio-gpu scanout" : "software buffer (no scanout)";
}

void c26_framebuffer_present(void)
{
    if (!gpu_online) {
        return;
    }
    gpu_transfer_t transfer = {
        {VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D, 0, 0, 0, 0},
        {0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT},
        0,
        GPU_RESOURCE_ID,
        0,
    };
    if (!gpu_command(&transfer, sizeof(transfer))) {
        gpu_online = 0;
        return;
    }
    gpu_flush_t flush = {
        {VIRTIO_GPU_CMD_RESOURCE_FLUSH, 0, 0, 0, 0},
        {0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT},
        GPU_RESOURCE_ID,
        0,
    };
    if (!gpu_command(&flush, sizeof(flush))) {
        gpu_online = 0;
    }
}

void c26_draw_pixel(int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < (int)C26_SCREEN_WIDTH &&
        y < (int)C26_SCREEN_HEIGHT) {
        pixels[(unsigned int)y * C26_SCREEN_WIDTH + (unsigned int)x] = color;
    }
}

void c26_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)C26_SCREEN_WIDTH) {
        x1 = C26_SCREEN_WIDTH;
    }
    if (y1 > (int)C26_SCREEN_HEIGHT) {
        y1 = C26_SCREEN_HEIGHT;
    }
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            pixels[(unsigned int)py * C26_SCREEN_WIDTH + (unsigned int)px] = color;
        }
    }
}

void c26_draw_rect(int x, int y, int width, int height, uint32_t color)
{
    c26_fill_rect(x, y, width, 1, color);
    c26_fill_rect(x, y + height - 1, width, 1, color);
    c26_fill_rect(x, y, 1, height, color);
    c26_fill_rect(x + width - 1, y, 1, height, color);
}

void c26_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        c26_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

const uint8_t *c26_font_glyph(char ch);

static const uint8_t *glyph_for(char ch)
{
    return c26_font_glyph(ch);
}

const uint8_t *c26_font_glyph(char ch)
{
    static const uint8_t unknown[5] = {0x7f, 0x41, 0x5d, 0x41, 0x7f};
    /* Column-major 5x7 glyphs, bit 0 = top row, for ASCII 32..126. */
    static const uint8_t glyphs[95][5] = {
        {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},
        {0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
        {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},
        {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
        {0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},
        {0x08,0x2a,0x1c,0x2a,0x08},{0x08,0x08,0x3e,0x08,0x08},
        {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
        {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
        {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
        {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
        {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},
        {0x32,0x49,0x79,0x41,0x3e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
        {0x00,0x7f,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},
        {0x00,0x41,0x41,0x7f,0x00},{0x04,0x02,0x01,0x02,0x04},
        {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},
        {0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},
        {0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7f},
        {0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},
        {0x0c,0x52,0x52,0x52,0x3e},{0x7f,0x08,0x04,0x04,0x78},
        {0x00,0x44,0x7d,0x40,0x00},{0x20,0x40,0x44,0x3d,0x00},
        {0x7f,0x10,0x28,0x44,0x00},{0x00,0x41,0x7f,0x40,0x00},
        {0x7c,0x04,0x18,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},
        {0x38,0x44,0x44,0x44,0x38},{0x7c,0x14,0x14,0x14,0x08},
        {0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},
        {0x48,0x54,0x54,0x54,0x20},{0x04,0x3f,0x44,0x40,0x20},
        {0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},
        {0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},
        {0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},
        {0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7f,0x00,0x00},
        {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
    };
    if (ch >= ' ' && ch <= '~') {
        return glyphs[ch - ' '];
    }
    return unknown;
}

void c26_draw_char(int x, int y, char ch, uint32_t foreground,
                   uint32_t background, unsigned int scale)
{
    const uint8_t *glyph = glyph_for(ch);
    if (scale == 0) {
        scale = 1;
    }
    c26_fill_rect(x, y, (int)(6 * scale), (int)(8 * scale), background);
    for (unsigned int column = 0; column < 5; column++) {
        for (unsigned int row = 0; row < 7; row++) {
            if ((glyph[column] & (1U << row)) != 0) {
                c26_fill_rect(x + (int)(column * scale),
                              y + (int)(row * scale),
                              (int)scale, (int)scale, foreground);
            }
        }
    }
}

void c26_draw_text(int x, int y, const char *text, uint32_t foreground,
                   uint32_t background, unsigned int scale)
{
    while (*text != '\0') {
        c26_draw_char(x, y, *text++, foreground, background, scale);
        x += (int)(6 * scale);
    }
}
