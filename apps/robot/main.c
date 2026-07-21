/* ROBOT: a robot control panel driving the device fabric. Four channels
 * ("motors/servos") are mapped to device registers 0x10..0x13. Each channel
 * is a horizontal bar whose fill length tracks its 0..255 value. Up/Down
 * select a channel; Left/Right nudge the selected channel by a step. Every
 * change is written to the fabric register and read straight back, proving
 * the control path round-trips. Talks to the machine only through c26_api_t. */

#include "c26_api.h"

#define CHANNELS 4
#define STEP 16       /* how far Left/Right moves a channel value */
#define PANEL_TOP 34  /* first bar sits below the title bar */

static uint32_t width;
static uint32_t height;

/* Last values written to each channel, mirrored for display. */
static uint8_t chan[CHANNELS];

/* Fabric register backing each channel: 0x10..0x13. */
static const uint16_t reg[CHANNELS] = {0x10, 0x11, 0x12, 0x13};

static const char *labels[CHANNELS] = {
    "MOTOR A", "MOTOR B", "SERVO 1", "SERVO 2",
};

static int sel; /* currently selected channel */

/* Base-10 formatting: no libc, so do it by hand like CALC's format_int. */
static void format_int(int64_t value, char *out)
{
    char tmp[24];
    int n = 0;
    int negative = value < 0;
    uint64_t u = negative ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    if (u == 0) tmp[n++] = '0';
    while (u != 0) {
        tmp[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    }
    int k = 0;
    if (negative) out[k++] = '-';
    while (n > 0) out[k++] = tmp[--n];
    out[k] = '\0';
}

/* Layout: each channel occupies one row in the panel below the title bar. */
static void bar_rect(int i, int *x, int *y, int *w, int *h)
{
    int rows = CHANNELS;
    int rh = ((int)height - PANEL_TOP) / rows;
    *x = 90;
    *y = PANEL_TOP + i * rh + 4;
    *w = (int)width - *x - 12;
    *h = rh - 12;
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

static void draw(const c26_api_t *api)
{
    vgrad(api, 0, 0, (int)width, (int)height, 0x141a36, 0x0a0e20);
    /* glossy title bar: gradient, bright top highlight, dark seam shadow */
    vgrad(api, 0, 0, (int)width, 24, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x4a56b4);
    api->fill_rect(0, 23, (int)width, 1, 0x0d1130);
    label_fg(api, 6, 4, "ROBOT", 0xffffff, 0x30397e, 2);

    for (int i = 0; i < CHANNELS; i++) {
        int x, y, w, h;
        bar_rect(i, &x, &y, &w, &h);

        int selected = (i == sel);
        uint32_t lbl = selected ? 0xffd34d : 0x9fb0d0;

        /* Channel label to the left of its bar. */
        label_fg(api, 6, y + h / 2 - 7, labels[i], lbl, 0x0b1025, 1);

        /* Recessed track gradient with a top seam shadow. */
        vgrad(api, x, y, w, h, 0x141a36, 0x0a0e20);
        api->fill_rect(x, y, w, 1, 0x05070f);
        /* Beveled gauge fill scaled from the 0..255 value. */
        int fill = (int)((uint32_t)chan[i] * (uint32_t)w / 255U);
        uint32_t bar = selected ? 0x68f0c0 : 0x35709a;
        if (fill > 0) {
            vgrad(api, x, y, fill, h, cmix(bar, 0xffffff, 70),
                  cmix(bar, 0x000000, 60));
            api->fill_rect(x, y, fill, 1, cmix(bar, 0xffffff, 140));
            api->fill_rect(x, y + h - 1, fill, 1, cmix(bar, 0x000000, 120));
        }

        /* Highlight the selected channel with an outline. */
        if (selected) api->draw_rect(x - 2, y - 2, w + 4, h + 4, 0xffd34d);

        /* Numeric value at the right edge of the bar. */
        char buf[8];
        format_int(chan[i], buf);
        label_fg(api, x + w - 26, y + h / 2 - 7, buf, 0xffffff, 0x1a2140, 1);
    }
}

/* Push the selected channel's value through the device fabric and read it
 * back, updating the mirror from what the fabric returns. Reports on serial. */
static void commit(const c26_api_t *api)
{
    api->dev_write8(reg[sel], chan[sel]);
    api->puts("ROBOT SET ");
    api->put_int(reg[sel]);
    api->putc(' ');
    api->put_int(chan[sel]);
    api->putc('\n');

    uint8_t got = 0;
    api->dev_read8(reg[sel], &got);
    if (got == chan[sel]) {
        api->puts("ROBOT GET OK\n");
    } else {
        api->puts("ROBOT GET MISMATCH\n");
    }
    chan[sel] = got; /* display reflects what the fabric actually holds */
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("ROBOT CART ONLINE\n");
    draw(api);
    api->present();

    int dirty = 0;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;

        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;

        if (ch == C26_KEY_UP) {
            sel = (sel + CHANNELS - 1) % CHANNELS;
            dirty = 1;
        } else if (ch == C26_KEY_DOWN) {
            sel = (sel + 1) % CHANNELS;
            dirty = 1;
        } else if (ch == C26_KEY_LEFT) {
            int v = (int)chan[sel] - STEP;
            if (v < 0) v = 0;
            chan[sel] = (uint8_t)v;
            commit(api);
            dirty = 1;
        } else if (ch == C26_KEY_RIGHT) {
            int v = (int)chan[sel] + STEP;
            if (v > 255) v = 255;
            chan[sel] = (uint8_t)v;
            commit(api);
            dirty = 1;
        }

        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 2) {
            last_present = now;
            dirty = 0;
            draw(api);
            api->present();
        }
        api->idle();
    }
    api->puts("ROBOT CART EXIT\n");
    return 0;
}
