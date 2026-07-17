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

static int field_top(void) { return 24; }
static int field_h(void) { return (int)ui.height - 40; }

static void reset_ball(void)
{
    ball_x = (int)ui.width / 2;
    ball_y = field_top() + 90;
    ball_dx = 3;
    ball_dy = 3;
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
    ui_titlebar(&ui, title, "R RESTART  Q QUIT");
    int bw = ((int)ui.width - 12) / BRICK_COLS;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!bricks[r][c]) continue;
            static const uint32_t colors[4] = {0xb86962, 0xffad45, 0xbfce72,
                                               0x68f0c0};
            api->fill_rect(6 + c * bw, field_top() + 6 + r * 14, bw - 3, 11,
                           colors[r]);
        }
    }
    api->fill_rect(paddle_x, field_top() + field_h() - 8, 56, 6, UI_BRIGHT);
    api->fill_rect(ball_x - 3, ball_y - 3, 6, 6, UI_WARN);
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
    int left = 6, right = (int)ui.width - 6;
    int top = field_top() + 4, bottom = field_top() + field_h();
    if (ball_x <= left || ball_x >= right) {
        ball_dx = -ball_dx;
        blip(api, 330);
    }
    if (ball_y <= top) {
        ball_dy = -ball_dy;
        blip(api, 330);
    }
    int paddle_y = bottom - 8;
    if (ball_y >= paddle_y - 3 && ball_y <= paddle_y + 3 &&
        ball_x >= paddle_x - 3 && ball_x <= paddle_x + 59 && ball_dy > 0) {
        ball_dy = -ball_dy;
        ball_dx += (ball_x - (paddle_x + 28)) / 12;
        if (ball_dx > 5) ball_dx = 5;
        if (ball_dx < -5) ball_dx = -5;
        if (ball_dx == 0) ball_dx = 1;
        blip(api, 262);
    }
    int bw = ((int)ui.width - 12) / BRICK_COLS;
    int brick_row = (ball_y - field_top() - 6) / 14;
    int brick_col = (ball_x - 6) / bw;
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
        if (mouse_x >= 0 && mouse_x < (int)ui.width - 56) paddle_x = mouse_x;
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
