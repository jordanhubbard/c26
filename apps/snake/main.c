/* SNAKE: the classic. Arrow keys steer a snake around a grid; it grows by one
 * cell each time it eats a piece of food and the score climbs. Running into a
 * wall or into its own body ends the game; any key restarts. Q or Esc quits.
 * The snake crawls on its own from the 100 Hz tick, so it plays (and dies, and
 * scores) even with no input. Talks to the machine only through c26_api_t. */

#include "c26_api.h"

#define CELL 16          /* pixel size of one grid cell */
#define TOP 24           /* title bar height; play field begins below it */
#define STEP_TICKS 8     /* ticks between snake moves at 100 Hz (~12.5/s) */
#define MAX_LEN 512      /* body[] capacity */

static uint32_t width;
static uint32_t height;

static int grid_w;       /* play field size in cells */
static int grid_h;

/* The snake body. body[0] is the head; len segments are live. */
static struct { int x, y; } body[MAX_LEN];
static int len;

static int dir_x, dir_y; /* current heading, one of the four unit vectors */
static int food_x, food_y;
static int score;
static int game_over;

static uint64_t seed;    /* LCG state for food placement */

static void format_int(int value, char *out)
{
    char tmp[16];
    int n = 0;
    int negative = value < 0;
    unsigned int u = negative ? (unsigned int)(-value) : (unsigned int)value;
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

/* Advance the LCG and return the next value. */
static uint64_t next_rand(void)
{
    seed = seed * 6364136223846793005ULL + 1ULL;
    return seed;
}

/* Place food on a random empty cell (retry if it lands on the snake). */
static void place_food(void)
{
    for (int tries = 0; tries < 256; tries++) {
        int fx = (int)((next_rand() >> 33) % (uint64_t)grid_w);
        int fy = (int)((next_rand() >> 33) % (uint64_t)grid_h);
        int clash = 0;
        for (int i = 0; i < len; i++) {
            if (body[i].x == fx && body[i].y == fy) { clash = 1; break; }
        }
        if (!clash) { food_x = fx; food_y = fy; return; }
    }
    /* Field is nearly full; take whatever the last draw gave us. */
}

static void reset_game(void)
{
    len = 3;
    int cx = grid_w / 2;
    int cy = grid_h / 2;
    /* Lay the body out horizontally with the head at [0]. */
    for (int i = 0; i < len; i++) {
        body[i].x = cx - i;
        body[i].y = cy;
    }
    dir_x = 1;   /* start crawling right so it moves without input */
    dir_y = 0;
    score = 0;
    game_over = 0;
    place_food();
    /* Nudge the initial heading toward the food only in directions that don't
       run back into the body (which lies to the left of the head): up, down,
       or keep the default rightward crawl. Never steer left into the neck. */
    if (food_x == cx && food_y < cy) { dir_x = 0; dir_y = -1; }
    else if (food_x == cx && food_y > cy) { dir_x = 0; dir_y = 1; }
}

/* Change heading, refusing a direct 180-degree reversal into the neck. */
static void set_dir(int nx, int ny)
{
    if (nx == -dir_x && ny == -dir_y) return;
    dir_x = nx;
    dir_y = ny;
}

/* One grid step. Reports scoring and game-over on the serial line. */
static void step(const c26_api_t *api)
{
    if (game_over) return;

    int hx = body[0].x + dir_x;
    int hy = body[0].y + dir_y;

    /* Wall collision. */
    if (hx < 0 || hx >= grid_w || hy < 0 || hy >= grid_h) {
        game_over = 1;
        api->puts("SNAKE GAME OVER\n");
        return;
    }

    int eating = (hx == food_x && hy == food_y);

    /* Self collision. The tail cell is about to vacate unless we grow, so
       ignore it in the check when not eating. */
    int check = eating ? len : len - 1;
    for (int i = 0; i < check; i++) {
        if (body[i].x == hx && body[i].y == hy) {
            game_over = 1;
            api->puts("SNAKE GAME OVER\n");
            return;
        }
    }

    /* Shift the body along, then drop the new head in front. */
    if (eating && len < MAX_LEN) len++;
    for (int i = len - 1; i > 0; i--) body[i] = body[i - 1];
    body[0].x = hx;
    body[0].y = hy;

    if (eating) {
        score++;
        api->puts("SNAKE SCORE ");
        api->put_int(score);
        api->putc('\n');
        place_food();
    }
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

/* Draw one glossy beveled cell (bright top fading to dark bottom). */
static void gloss_cell(const c26_api_t *api, int px, int py, int size,
                       uint32_t base)
{
    vgrad(api, px, py, size, size, cmix(base, 0xffffff, 60),
          cmix(base, 0x000000, 55));
    api->fill_rect(px, py, size, 2, cmix(base, 0xffffff, 130));
    api->fill_rect(px, py + size - 1, size, 1, cmix(base, 0x000000, 150));
}

static void draw(const c26_api_t *api)
{
    /* Playfield: a subtle vertical gradient with a very faint grid tint so the
       cells read without stealing attention from the snake. */
    vgrad(api, 0, 0, (int)width, (int)height, 0x0c1330, 0x060a1c);
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = (gy & 1); gx < grid_w; gx += 2) {
            api->fill_rect(gx * CELL, TOP + gy * CELL, CELL - 1, CELL - 1,
                           0x0d1636);
        }
    }

    /* Glossy title/score bar: bright top highlight, dark seam shadow. */
    vgrad(api, 0, 0, (int)width, TOP, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x5866c4);
    api->fill_rect(0, TOP - 1, (int)width, 1, 0x04060e);
    label_fg(api, 6, 4, "SNAKE", 0xffffff, 0x30397e, 2);

    char buf[16];
    format_int(score, buf);
    label_fg(api, (int)width - 70, 4, "SCORE", 0x9fb0ff, 0x30397e, 1);
    label_fg(api, (int)width - 70, 13, buf, 0x68f0c0, 0x30397e, 1);

    /* Food: a glossy red disc with a bright specular highlight. */
    {
        int fx = food_x * CELL;
        int fy = TOP + food_y * CELL;
        gloss_cell(api, fx, fy, CELL - 1, 0xff5f57);
        /* Little specular spot near the top-left. */
        api->fill_rect(fx + 3, fy + 3, 3, 3, cmix(0xff5f57, 0xffffff, 190));
    }

    /* Snake: glossy beveled beads; a distinct brighter head with an eye. */
    for (int i = 0; i < len; i++) {
        int px = body[i].x * CELL;
        int py = TOP + body[i].y * CELL;
        uint32_t base = (i == 0) ? 0x68f0c0 : 0x2fbf5a;
        gloss_cell(api, px, py, CELL - 1, base);
        if (i == 0) {
            /* A tiny eye highlight on the head. */
            api->fill_rect(px + CELL / 2, py + 3, 2, 2, 0x0a1a12);
        }
    }

    if (game_over) {
        int bx = (int)width / 2 - 90;
        int by = (int)height / 2 - 24;
        vgrad(api, bx, by, 180, 48, 0x232b58, 0x11162e);
        api->fill_rect(bx, by, 180, 1, 0x5866c4);
        api->draw_rect(bx, by, 180, 48, 0xff5f57);
        label_fg(api, bx + 18, by + 6, "GAME OVER", 0xffd34d, 0x1a2140, 2);
        label_fg(api, bx + 20, by + 30, "PRESS A KEY", 0x9fb0ff, 0x1a2140, 1);
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("SNAKE CART ONLINE\n");

    grid_w = (int)width / CELL;
    grid_h = ((int)height - TOP) / CELL;

    seed = api->ticks() ^ 0x9e3779b97f4a7c15ULL;
    reset_game();
    draw(api);
    api->present();

    uint64_t next_step = api->ticks() + STEP_TICKS;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;

        int ch = api->getchar();
        if (ch == 'Q' || ch == 'q' || ch == 0x1b) break;
        if (ch > 0) {
            if (game_over) {
                /* Any key restarts once the game has ended. */
                reset_game();
            } else if (ch == C26_KEY_UP) {
                set_dir(0, -1);
            } else if (ch == C26_KEY_DOWN) {
                set_dir(0, 1);
            } else if (ch == C26_KEY_LEFT) {
                set_dir(-1, 0);
            } else if (ch == C26_KEY_RIGHT) {
                set_dir(1, 0);
            }
        }

        uint64_t now = api->ticks();
        if (now >= next_step) {
            next_step = now + STEP_TICKS;
            step(api);
        }

        /* Throttled present, same cadence as the other carts. */
        if (now - last_present >= 2) {
            last_present = now;
            draw(api);
            api->present();
        }
        api->idle();
    }

    api->puts("SNAKE CART EXIT\n");
    return 0;
}
