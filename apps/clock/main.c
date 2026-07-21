/* CLOCK: a digital wall-clock. Shows the current time as HH:MM:SS in big
 * centered text, refreshed once a second from the real-time clock (ABI v4).
 * Q or Esc quits. Talks to the machine only through the c26_api_t vector.
 * Prints CLOCK <HH:MM:SS> once at startup so the time is checkable. */

#include "c26_api.h"

#define TIME_SCALE 5      /* glyphs are 6*scale wide, 8*scale tall */
#define GLYPH_W 6
#define GLYPH_H 8

static uint32_t width;
static uint32_t height;

/* Wall-clock seconds since the epoch, or a ticks-based fallback on old ABIs. */
static uint64_t clock_seconds(const c26_api_t *api)
{
    if (api->version >= 4) {
        return api->rtc_seconds();
    }
    return api->ticks() / 100U; /* 100 Hz ticks -> seconds elapsed */
}

/* Fill out[] with "HH:MM:SS\0" derived from seconds since the epoch. */
static void format_time(uint64_t secs, char *out)
{
    unsigned int s = (unsigned int)(secs % 60U);
    unsigned int m = (unsigned int)((secs / 60U) % 60U);
    unsigned int h = (unsigned int)((secs / 3600U) % 24U);
    out[0] = (char)('0' + h / 10U);
    out[1] = (char)('0' + h % 10U);
    out[2] = ':';
    out[3] = (char)('0' + m / 10U);
    out[4] = (char)('0' + m % 10U);
    out[5] = ':';
    out[6] = (char)('0' + s / 10U);
    out[7] = (char)('0' + s % 10U);
    out[8] = '\0';
}

static uint32_t cmix(uint32_t a, uint32_t b, int t)
{
    int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
    int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return ((uint32_t)(ra + (rb - ra) * t / 256) << 16) |
           ((uint32_t)(ga + (gb - ga) * t / 256) << 8) |
           (uint32_t)(ba + (bb - ba) * t / 256);
}
static void vgrad(const c26_api_t *api, int x, int y, int w, int h, uint32_t top,
                  uint32_t bot)
{
    for (int i = 0; i < h; i++)
        api->fill_rect(x, y + i, w, 1, cmix(top, bot, i * 256 / (h > 0 ? h : 1)));
}
static void label_fg(const c26_api_t *api, int x, int y, const char *s,
                     uint32_t fg, uint32_t bg, int scale)
{
    if (api->version >= 6)
        api->text_fg(x, y, s, fg, (unsigned int)scale);
    else
        api->text(x, y, s, fg, bg, scale);
}

static void draw(const c26_api_t *api, const char *time_str)
{
    vgrad(api, 0, 0, (int)width, (int)height, 0x141a36, 0x0a0e20);
    /* glossy title bar: gradient, bright top highlight, dark seam shadow */
    vgrad(api, 0, 0, (int)width, 24, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x4a56b4);
    api->fill_rect(0, 23, (int)width, 1, 0x0d1130);
    label_fg(api, 6, 4, "CLOCK", 0xffffff, 0x30397e, 2);

    /* Center the eight-character time string in the window body. */
    int text_w = 8 * GLYPH_W * TIME_SCALE;
    int text_h = GLYPH_H * TIME_SCALE;
    int x = ((int)width - text_w) / 2;
    int y = 24 + ((int)height - 24 - text_h) / 2;
    if (x < 0) x = 0;
    if (y < 24) y = 24;

    /* Recessed gradient panel behind the digits, with a subtle bevel. */
    int px = x - 16, py = y - 16, pw = text_w + 32, ph = text_h + 32;
    vgrad(api, px, py, pw, ph, 0x0c1226, 0x060a18);
    api->fill_rect(px, py, pw, 1, 0x04060e);
    api->fill_rect(px, py + ph - 1, pw, 1, 0x1c2450);

    label_fg(api, x, y, time_str, 0x68f0c0, 0x0c1226, TIME_SCALE);
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("CLOCK CART ONLINE\n");

    char time_str[9];
    uint64_t last_secs = clock_seconds(api);
    format_time(last_secs, time_str);

    /* Emit the current time once so tests can gate on it. */
    api->puts("CLOCK ");
    api->puts(time_str);
    api->putc('\n');

    draw(api, time_str);
    api->present();

    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;

        /* Re-present only when the displayed second changes. */
        uint64_t secs = clock_seconds(api);
        if (secs != last_secs) {
            last_secs = secs;
            format_time(secs, time_str);
            draw(api, time_str);
            api->present();
        }
        api->idle();
    }

    api->puts("CLOCK CART EXIT\n");
    return 0;
}
