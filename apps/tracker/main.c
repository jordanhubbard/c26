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

static void draw(const c26_api_t *api)
{
    ui_clear(&ui);
    ui_titlebar(&ui, "TRACKER", "SPC PLAY  A/Z NOTE  ^S SAVE  Q QUIT");
    for (int voice = 0; voice < VOICES; voice++) {
        int y = 42 + voice * 34;
        char label[4] = {'V', (char)('0' + voice), 0, 0};
        api->text(8, y + 6, label, UI_TEXT, UI_BG, 2);
        for (int step = 0; step < STEPS; step++) {
            int x = 44 + step * 48;
            uint32_t bg = UI_BG;
            if (playing && step == play_step) bg = 0x2c5a3c;
            if (voice == cursor_voice && step == cursor_step) bg = UI_ACCENT;
            api->fill_rect(x, y, 44, 30, bg);
            uint8_t note = grid[voice][step];
            if (note != 0) {
                char cell[4];
                cell[0] = note_names[(note - 1) % 12][0];
                cell[1] = note_names[(note - 1) % 12][1];
                cell[2] = (char)('3' + (note - 1) / 12);
                cell[3] = '\0';
                api->text(x + 4, y + 7, cell, UI_BRIGHT, bg, 2);
            } else {
                api->text(x + 10, y + 7, "..", 0x505a8c, bg, 2);
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
