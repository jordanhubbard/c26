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

static void draw(const c26_api_t *api, const char *time_str)
{
    api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
    api->fill_rect(0, 0, (int)width, 24, 0x222957);
    api->text(6, 4, "CLOCK", 0xffffff, 0x222957, 2);

    /* Center the eight-character time string in the window body. */
    int text_w = 8 * GLYPH_W * TIME_SCALE;
    int text_h = GLYPH_H * TIME_SCALE;
    int x = ((int)width - text_w) / 2;
    int y = 24 + ((int)height - 24 - text_h) / 2;
    if (x < 0) x = 0;
    if (y < 24) y = 24;
    api->text(x, y, time_str, 0x68f0c0, 0x0b1025, TIME_SCALE);
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
