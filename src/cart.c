#include "c26.h"
#include "c26_api.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_fs.h"
#include "c26_graphics.h"

static int api_getchar(void)
{
    char ch;
    return c26_basic_key_pop(&ch) ? (int)(uint8_t)ch : -1;
}

static int api_voice_start(unsigned int voice, int wave, uint32_t frequency_hz,
                           uint8_t volume, uint8_t pan)
{
    if (wave < 0 || wave > 3) {
        return 0;
    }
    return c26_audio_voice_start(voice, (c26_waveform_t)wave, frequency_hz,
                                 volume, pan);
}

static void api_idle(void)
{
    c26_io_pump();
    c26_idle();
}

static const c26_api_t api = {
    .version = C26_CART_VERSION,
    .puts = c26_puts,
    .putc = c26_putc,
    .put_int = c26_put_int,
    .getchar = api_getchar,
    .mouse = c26_desktop_mouse,
    .stop_requested = c26_basic_break_requested,
    .ticks = c26_interrupt_ticks,
    .idle = api_idle,
    .framebuffer = c26_framebuffer_pixels,
    .pixel = c26_draw_pixel,
    .fill_rect = c26_fill_rect,
    .draw_rect = c26_draw_rect,
    .line = c26_draw_line,
    .text = c26_draw_text,
    .present = c26_framebuffer_present,
    .voice_start = api_voice_start,
    .voice_stop = c26_audio_voice_stop,
    .fs_save = c26_fs_save,
    .fs_load = c26_fs_load,
    .fs_delete = c26_fs_delete,
    .fs_count = c26_fs_count,
    .fs_entry = c26_fs_entry,
    .dev_read8 = c26_device_read8,
    .dev_write8 = c26_device_write8,
};

int c26_cart_run(const char *name)
{
    uint8_t *base = (uint8_t *)C26_CART_BASE;
    size_t size = 0;
    if (!c26_fs_load(name, base, C26_CART_MAX_BYTES, &size)) {
        c26_puts("Error: cartridge load failed\n");
        return -1;
    }
    const c26_cart_header_t *header = (const c26_cart_header_t *)base;
    if (size < sizeof(*header) || header->magic != C26_CART_MAGIC ||
        header->version != C26_CART_VERSION || header->load_size > size ||
        header->entry_offset >= header->load_size ||
        header->load_size + (uint64_t)header->bss_size > C26_CART_MAX_BYTES) {
        c26_puts("Error: not a c26 cartridge\n");
        return -1;
    }
    memset(base + header->load_size, 0, header->bss_size);
    __asm__ volatile("fence.i");

    int (*entry)(const c26_api_t *) =
        (int (*)(const c26_api_t *))(base + header->entry_offset);

    c26_screen_mode_t previous_mode = c26_screen_mode();
    c26_basic_set_external_consumer(1);
    c26_basic_clear_break();
    c26_screen_set_mode(C26_SCREEN_CART);
    c26_puts("CART START ");
    c26_puts(name);
    c26_putc('\n');

    int result = entry(&api);

    c26_basic_set_external_consumer(0);
    c26_basic_clear_break();
    c26_screen_set_mode(previous_mode == C26_SCREEN_DESKTOP
                            ? C26_SCREEN_DESKTOP
                            : C26_SCREEN_CONSOLE);
    c26_puts("CART EXIT ");
    c26_put_int(result);
    c26_putc('\n');
    c26_console_flush();
    return result;
}
