/* TRACKER: an 8-voice step sequencer for the c26 mixer — the SID
 * successor's front panel. Arrows move the cell cursor, A/Z raise and
 * lower the note, 0 clears, Space plays/stops, Ctrl-S saves the pattern
 * to C26FS (PATTERN), Q quits. Each voice row uses its own waveform. */

#include "ui.h"

#define VOICES 8
#define STEPS 16
#define TICKS_PER_STEP 12

static c26_ui_t ui;
static uint8_t grid[VOICES][STEPS];
static int cursor_voice;
static int cursor_step;
static int playing;
static int play_step;
static uint64_t next_step_tick;

static const uint16_t freq[25] = {
    0,   131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
};
static const char *note_names[12] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-",
};

/* Per-voice hues for lit step cells, so each row reads as its own timbre. */
static const uint32_t voice_hue[VOICES] = {
    0x2f9a54, 0x2f8a9a, 0x3f5ab4, 0x7a46b4,
    0xb44688, 0xb46a34, 0x9aa030, 0x46b46a,
};

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
    ui_clear(&ui);
    /* glossy backdrop behind the grid, matching the desktop's Aqua wash */
    vgrad(api, 0, 0, (int)ui.width, (int)ui.height, 0x141a36, 0x0b1025);
    ui_titlebar(&ui, "TRACKER", "SPC PLAY  A/Z NOTE  ^S SAVE  Q QUIT");
    for (int voice = 0; voice < VOICES; voice++) {
        int y = 42 + voice * 34;
        uint32_t vh = voice_hue[voice];
        char label[4] = {'V', (char)('0' + voice), 0, 0};
        /* voice chip: beveled gradient tag in the voice's hue */
        vgrad(api, 4, y, 34, 30, cmix(vh, 0xffffff, 55), cmix(vh, 0x000000, 70));
        api->fill_rect(4, y, 34, 1, cmix(vh, 0xffffff, 140));
        api->fill_rect(4, y + 29, 34, 1, cmix(vh, 0x000000, 150));
        label_fg(api, 8, y + 6, label, 0xffffff, vh, 2);
        for (int step = 0; step < STEPS; step++) {
            int x = 44 + step * 48;
            uint8_t note = grid[voice][step];
            int is_cursor = (voice == cursor_voice && step == cursor_step);
            int is_play = (playing && step == play_step);
            /* lit steps glow in the voice hue; empty steps stay a dark panel */
            uint32_t base = note != 0 ? vh : 0x141a36;
            if (is_play) base = cmix(base, 0xffffff, 55); /* playhead glow */
            if (is_cursor) base = 0x4653b4;               /* cursor accent */
            uint32_t top = cmix(base, 0xffffff, is_cursor ? 90 : 60);
            uint32_t bot = cmix(base, 0x000000, 60);
            vgrad(api, x, y, 44, 30, top, bot);
            api->fill_rect(x, y, 44, 2, cmix(base, 0xffffff, 130));     /* highlight */
            api->fill_rect(x, y + 29, 44, 1, cmix(base, 0x000000, 150)); /* seam */
            if (note != 0) {
                char cell[4];
                cell[0] = note_names[(note - 1) % 12][0];
                cell[1] = note_names[(note - 1) % 12][1];
                cell[2] = (char)('3' + (note - 1) / 12);
                cell[3] = '\0';
                label_fg(api, x + 4, y + 7, cell, 0xffffff, base, 2);
            } else {
                label_fg(api, x + 10, y + 7, "..",
                         is_cursor ? 0xdfe4ff : 0x6a76b0, base, 2);
            }
        }
    }
    ui_status(&ui, playing ? "PLAYING" : "STOPPED", playing ? UI_GOOD : UI_TEXT);
}

static void silence(const c26_api_t *api)
{
    for (int voice = 0; voice < VOICES; voice++) {
        api->voice_stop((unsigned int)voice);
    }
}

static void step_voices(const c26_api_t *api)
{
    for (int voice = 0; voice < VOICES; voice++) {
        uint8_t note = grid[voice][play_step];
        if (note != 0 && note < 25) {
            api->voice_start((unsigned int)voice, voice % 4, freq[note], 150,
                             (uint8_t)(voice % 2 ? 200 : 56));
        } else {
            api->voice_stop((unsigned int)voice);
        }
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    ui_init(&ui, api);
    api->puts("TRACKER CART ONLINE\n");
    size_t loaded = 0;
    api->fs_load("PATTERN", grid, sizeof(grid), &loaded);
    draw(api);

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) {
            silence(api);
            return 0;
        }
        if (playing && api->ticks() >= next_step_tick) {
            next_step_tick = api->ticks() + TICKS_PER_STEP;
            play_step = (play_step + 1) % STEPS;
            step_voices(api);
            draw(api);
        }
        if (key >= 0) {
            uint8_t *cell = &grid[cursor_voice][cursor_step];
            if (key == 'Q' || key == 0x1b) {
                silence(api);
                api->puts("TRACKER CART EXIT\n");
                return 0;
            } else if (key == ' ') {
                playing = !playing;
                play_step = STEPS - 1;
                next_step_tick = api->ticks();
                if (!playing) silence(api);
            } else if (key == C26_KEY_UP && cursor_voice > 0) cursor_voice--;
            else if (key == C26_KEY_DOWN && cursor_voice < VOICES - 1)
                cursor_voice++;
            else if (key == C26_KEY_LEFT && cursor_step > 0) cursor_step--;
            else if (key == C26_KEY_RIGHT && cursor_step < STEPS - 1)
                cursor_step++;
            else if (key == 'A' && *cell < 24) (*cell)++;
            else if (key == 'Z' && *cell > 0) (*cell)--;
            else if (key == '0' || key == '\b') *cell = 0;
            else if (key == 0x13) {
                if (api->fs_save("PATTERN", grid, sizeof(grid))) {
                    api->puts("SAVED PATTERN\n");
                }
            }
            draw(api);
        }
        ui_flush(&ui);
    }
}
