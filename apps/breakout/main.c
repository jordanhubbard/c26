/* BREAKOUT: the proof-of-life game. Mouse moves the paddle, the mixer
 * blips on every bounce, R restarts, Q quits. */

#include "ui.h"

#define BRICK_COLS 10
#define BRICK_ROWS 4

static c26_ui_t ui;
static int ball_x, ball_y, ball_dx, ball_dy;
static int paddle_x;
static int score;
static int lives;
static int running_game;
static uint8_t bricks[BRICK_ROWS][BRICK_COLS];
static uint64_t blip_until;

static int field_top(void) { return 36; }
static int field_h(void) { return (int)ui.height - 64; }

static void reset_ball(void)
{
    ball_x = (int)ui.width / 2;
    ball_y = field_top() + 180;
    ball_dx = 6;
    ball_dy = 6;
}

static void reset_game(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) bricks[r][c] = 1;
    score = 0;
    lives = 3;
    running_game = 1;
    reset_ball();
}

static void blip(const c26_api_t *api, uint32_t hz)
{
    api->voice_start(7, 0, hz, 120, 128);
    blip_until = api->ticks() + 4;
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
    ui_clear(&ui);
    char title[32] = "BREAKOUT  ";
    int used = 10;
    int value = score;
    char digits[8];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) title[used++] = digits[--count];
    title[used] = '\0';
    /* glossy title/score bar */
    int tw = (int)ui.width;
    vgrad(api, 0, 0, tw, 30, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, tw, 1, 0x4a56b4);  /* top highlight */
    api->fill_rect(0, 29, tw, 1, 0x0a0e20); /* seam shadow */
    label_fg(api, 8, 5, title, UI_BRIGHT, UI_PANEL, 3);
    const char *hint = "R RESTART  Q QUIT";
    int hint_len = 0;
    while (hint[hint_len] != '\0') hint_len++;
    label_fg(api, tw - 12 * hint_len - 8, 9, hint, UI_TEXT, UI_PANEL, 2);
    int bw = ((int)ui.width - 16) / BRICK_COLS;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!bricks[r][c]) continue;
            static const uint32_t colors[4] = {0xb86962, 0xffad45, 0xbfce72,
                                               0x68f0c0};
            int bx = 8 + c * bw, by = field_top() + 8 + r * 26;
            api->fill_rect(bx, by, bw - 5, 20, colors[r]);
            /* subtle bevel: bright top edge, dark bottom edge; colour kept */
            api->fill_rect(bx, by, bw - 5, 1, cmix(colors[r], 0xffffff, 90));
            api->fill_rect(bx, by + 19, bw - 5, 1, cmix(colors[r], 0x000000, 90));
        }
    }
    int px = paddle_x, py = field_top() + field_h() - 12;
    api->fill_rect(px, py, 100, 10, UI_BRIGHT);
    api->fill_rect(px, py, 100, 1, cmix(UI_BRIGHT, 0xffffff, 90));   /* top */
    api->fill_rect(px, py + 9, 100, 1, cmix(UI_BRIGHT, 0x000000, 90)); /* base */
    api->fill_rect(ball_x - 5, ball_y - 5, 10, 10, UI_WARN);
    char status[24] = "LIVES ";
    status[6] = (char)('0' + lives);
    status[7] = '\0';
    ui_status(&ui, running_game ? status : "GAME OVER - R RESTARTS",
              running_game ? UI_TEXT : UI_WARN);
}

static void step(const c26_api_t *api)
{
    if (!running_game) return;
    ball_x += ball_dx;
    ball_y += ball_dy;
    int left = 8, right = (int)ui.width - 8;
    int top = field_top() + 4, bottom = field_top() + field_h();
    if (ball_x <= left || ball_x >= right) {
        ball_dx = -ball_dx;
        blip(api, 330);
    }
    if (ball_y <= top) {
        ball_dy = -ball_dy;
        blip(api, 330);
    }
    int paddle_y = bottom - 12;
    if (ball_y >= paddle_y - 5 && ball_y <= paddle_y + 5 &&
        ball_x >= paddle_x - 5 && ball_x <= paddle_x + 105 && ball_dy > 0) {
        ball_dy = -ball_dy;
        ball_dx += (ball_x - (paddle_x + 50)) / 20;
        if (ball_dx > 9) ball_dx = 9;
        if (ball_dx < -9) ball_dx = -9;
        if (ball_dx == 0) ball_dx = 1;
        blip(api, 262);
    }
    int bw = ((int)ui.width - 16) / BRICK_COLS;
    int brick_row = (ball_y - field_top() - 8) / 26;
    int brick_col = (ball_x - 8) / bw;
    if (brick_row >= 0 && brick_row < BRICK_ROWS && brick_col >= 0 &&
        brick_col < BRICK_COLS && bricks[brick_row][brick_col]) {
        bricks[brick_row][brick_col] = 0;
        ball_dy = -ball_dy;
        score += 10;
        blip(api, 494);
    }
    if (ball_y > bottom) {
        lives--;
        blip(api, 131);
        if (lives <= 0) running_game = 0;
        else reset_ball();
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    ui_init(&ui, api);
    api->puts("BREAKOUT CART ONLINE\n");
    paddle_x = (int)ui.width / 2 - 28;
    reset_game();
    draw(api);
    uint64_t next_frame = 0;

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) {
            api->voice_stop(7);
            return 0;
        }
        if (key == 'Q' || key == 0x1b) {
            api->voice_stop(7);
            api->puts("BREAKOUT CART EXIT\n");
            return 0;
        }
        if (key == 'R') reset_game();
        int mouse_x, mouse_y, buttons;
        api->mouse(&mouse_x, &mouse_y, &buttons);
        (void)mouse_y;
        (void)buttons;
        if (mouse_x >= 0 && mouse_x < (int)ui.width - 100) paddle_x = mouse_x;
        uint64_t now = api->ticks();
        if (now >= next_frame) {
            next_frame = now + 2;
            step(api);
            if (blip_until != 0 && now >= blip_until) {
                api->voice_stop(7);
                blip_until = 0;
            }
            draw(api);
        }
        ui_flush(&ui);
    }
}
