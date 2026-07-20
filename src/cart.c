#include "c26.h"
#include "c26_api.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_net.h"
#include "c26_user.h"

/* The process host and window manager. Up to C26_NPROC cartridges run
 * concurrently, each a U-mode process with its own Sv39 space, its own
 * surface, and a movable window composited in z-order over the BASIC
 * console, which is the root layer. All cartridges link at C26_CART_BASE;
 * page tables map that VA to a different physical slot per process. The
 * kernel schedules round-robin time slices from the main loop; the timer
 * trap ends a slice, and faults, exits, Ctrl-C, and KILL end a process.
 * Jobs also exchange bounded messages through per-process mailboxes. */

#define C26_NPROC 4
#define USER_STACK_BYTES (64U * 1024U)
#define SLICE_TICKS 3
#define EXIT_SLICE 0x7fff0001L
#define SURFACE_PIXELS (C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT)

#define TITLE_HEIGHT 26
#define BORDER 1
#define WINDOW_DEFAULT_W 840
#define WINDOW_DEFAULT_H 620

/* The desktop menu bar (macOS-style, at the very top) and window chrome:
   three traffic-light dots on the left of each title bar and a resize grip. */
#define MENU_H 28
#define DOT 14
#define DOT_GAP 6
#define DOT_LEFT 10
#define COL_CLOSE 0xff5f57U  /* red   */
#define COL_MIN 0xfebc2eU    /* amber */
#define COL_ZOOM 0x28c840U   /* green */
#define GRIP 14
#define MIN_WIN_W 140
#define MIN_WIN_H 70
#define MAILBOX_SLOTS 4U
#define MESSAGE_MAX 240U

extern char c26_user_stub_base[];
extern char c26_user_stub_exit[];
extern char c26_user_api[];

typedef enum {
    PROC_FREE = 0,
    PROC_RUNNABLE = 1,
} proc_state_t;

typedef struct {
    uint8_t from;
    uint16_t length;
    uint8_t data[MESSAGE_MAX];
} message_t;

typedef struct {
    proc_state_t state;
    char name[C26_FS_NAME_MAX + 1];
    c26_user_frame_t frame;
    c26_vm_space_t space;
    int kill_pending;
    int surface_damaged;
    /* Window geometry: content is [0,w) x [0,h) of the surface. */
    int win_x;
    int win_y;
    int win_w;
    int win_h;
    int minimized; /* collapsed to just the title bar */
    int zoomed;    /* maximized to fill the desktop (green button) */
    int save_x, save_y, save_w, save_h; /* geometry before zoom */
    message_t mail[MAILBOX_SLOTS];
    unsigned int mail_head;
    unsigned int mail_tail;
} proc_t;

c26_user_frame_t *c26_current_frame;
uint64_t c26_kernel_trap_sp;
uint64_t c26_kernel_context[14];

static proc_t procs[C26_NPROC];
static uint8_t proc_stack[C26_NPROC][USER_STACK_BYTES]
    __attribute__((aligned(4096)));
static char proc_scratch[C26_NPROC][4096] __attribute__((aligned(4096)));
static uint32_t proc_surface[C26_NPROC][SURFACE_PIXELS]
    __attribute__((aligned(4096)));

static int current = -1;
static int focused = -1;
static int rr_cursor;
static int scene_dirty;
static volatile int slice_ticks_left;

static int z_order[C26_NPROC];
static int z_count;
static int dragging = -1;
static int drag_dx;
static int drag_dy;
static int resizing = -1;

/* The BASIC console is itself a full window on the desktop: movable, resizable,
   minimizable, zoomable — the same controls as any app — but never closed (it
   is the shell). focused < 0 means the console window has keyboard focus. */
static int con_x = 4;
static int con_y = MENU_H + 4;
static int con_w; /* content pixel size; lazily filled to the grid size */
static int con_h;
static int con_min;
static int con_zoom;
static int con_sx, con_sy, con_sw, con_sh; /* geometry saved before zoom */
static int con_dragging;
static int con_resizing;
static int con_drag_dx;
static int con_drag_dy;

/* The system clipboard: one shared text buffer any app (or BASIC via
   CLIP/PASTE) reads and writes, so copy/paste crosses the app boundary. */
#define CLIPBOARD_MAX 256
static char clipboard[CLIPBOARD_MAX];
static unsigned int clipboard_len;

void c26_clipboard_set(const char *data, unsigned int len)
{
    if (len > CLIPBOARD_MAX) len = CLIPBOARD_MAX;
    for (unsigned int i = 0; i < len; i++) clipboard[i] = data[i];
    clipboard_len = len;
}

unsigned int c26_clipboard_get(char *buf, unsigned int cap)
{
    unsigned int n = clipboard_len < cap ? clipboard_len : cap;
    for (unsigned int i = 0; i < n; i++) buf[i] = clipboard[i];
    return n;
}

unsigned int c26_clipboard_length(void) { return clipboard_len; }

/* The dock: a centred, floating, rounded launcher of app icons at the bottom,
   rebuilt from C26FS at boot. A click on an icon launches that cartridge. */
#define DOCK_H 74           /* reserved band at the screen bottom */
#define DOCK_PANEL_H 60     /* the floating rounded panel */
#define DOCK_TILE_W 56
#define DOCK_ICON 34
#define DOCK_MAX 24
typedef struct {
    char name[C26_FS_NAME_MAX + 1];
    int x; /* tile left */
    int w; /* tile width (DOCK_TILE_W) */
} dock_tile_t;
static dock_tile_t dock[DOCK_MAX];
static int dock_count;

static int dock_top(void) { return (int)C26_SCREEN_HEIGHT - DOCK_H; }

/* Scan C26FS and make a tile for every file that is a real cartridge (magic
   header), so the dock always reflects the launchable apps present at boot. */
void c26_dock_rebuild(void)
{
    dock_count = 0;
    size_t total = c26_fs_count();
    for (size_t i = 0; i < total && dock_count < DOCK_MAX; i++) {
        const char *name;
        uint32_t size;
        if (!c26_fs_entry(i, &name, &size)) continue;
        c26_cart_header_t header;
        if (c26_fs_peek(name, &header, sizeof(header)) < (int)sizeof(header)) {
            continue;
        }
        if (header.magic != C26_CART_MAGIC) continue;
        dock_tile_t *tile = &dock[dock_count++];
        int len = 0;
        while (name[len] != '\0' && len < (int)C26_FS_NAME_MAX) {
            tile->name[len] = name[len];
            len++;
        }
        tile->name[len] = '\0';
        tile->w = DOCK_TILE_W;
    }
    /* Centre the row of fixed-width tiles horizontally. */
    int start = ((int)C26_SCREEN_WIDTH - dock_count * DOCK_TILE_W) / 2;
    if (start < 4) start = 4;
    for (int i = 0; i < dock_count; i++) dock[i].x = start + i * DOCK_TILE_W;
}

/* Report each tile's centre as "DOCK <name> <x> <y>" so a scripted click
   (the smoke gate) can land on it without guessing the layout. */
void c26_dock_print(void)
{
    for (int i = 0; i < dock_count; i++) {
        c26_puts("DOCK ");
        c26_puts(dock[i].name);
        c26_putc(' ');
        c26_put_uint((uint64_t)(dock[i].x + dock[i].w / 2));
        c26_putc(' ');
        c26_put_uint((uint64_t)(dock_top() + DOCK_H / 2));
        c26_putc('\n');
    }
    if (dock_count == 0) c26_puts("DOCK EMPTY\n");
}

/* A filled rounded rectangle: each row inset near the top/bottom corners so
   the corners read as rounded (a circular quarter-arc). */
static void fill_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    for (int i = 0; i < h; i++) {
        int d = -1;
        if (i < r) d = r - i;
        else if (i >= h - r) d = r - (h - 1 - i);
        int inset = 0;
        if (d > 0) {
            while ((r - inset) * (r - inset) + d * d > r * r && inset < r) {
                inset++;
            }
        }
        c26_fill_rect(x + inset, y + i, w - 2 * inset, 1, color);
    }
}

static const uint32_t dock_palette[8] = {
    0x3d80ff, 0xffd34d, 0xff5ca8, 0x34c992,
    0x9b6cff, 0xff8a3d, 0x2fd0d8, 0xf05a7a,
};

static void dock_draw(void)
{
    if (dock_count == 0) return;
    int panel_x = dock[0].x - 8;
    int panel_w = dock_count * DOCK_TILE_W + 16;
    int panel_y = (int)C26_SCREEN_HEIGHT - DOCK_PANEL_H - 6;
    /* the floating, rounded, slightly translucent-looking dock panel */
    fill_round_rect(panel_x - 1, panel_y - 1, panel_w + 2, DOCK_PANEL_H + 2, 14,
                    0x3a4488);
    fill_round_rect(panel_x, panel_y, panel_w, DOCK_PANEL_H, 14, 0x1a2044);
    for (int i = 0; i < dock_count; i++) {
        dock_tile_t *tile = &dock[i];
        int ix = tile->x + (DOCK_TILE_W - DOCK_ICON) / 2;
        int iy = panel_y + 6;
        uint32_t c = dock_palette[i % 8];
        fill_round_rect(ix, iy, DOCK_ICON, DOCK_ICON, 6, c);
        /* a bright initial on the icon */
        char initial[2] = {tile->name[0], '\0'};
        c26_draw_text(ix + DOCK_ICON / 2 - 5, iy + DOCK_ICON / 2 - 7, initial,
                      0xffffff, c, 2);
        /* the app name in small text (6px/char), centred under the icon */
        int nlen = 0;
        while (tile->name[nlen] != '\0') nlen++;
        int label_x = tile->x + DOCK_TILE_W / 2 - nlen * 3;
        c26_draw_text(label_x, panel_y + DOCK_PANEL_H - 12, tile->name,
                      0xbac4ff, 0x1a2044, 1);
    }
}

/* A click in the dock strip launches the tile under the pointer. Returns 1
   if the click was inside the dock (so window hit-testing is skipped). */
static int dock_click(int x, int y)
{
    if (y < dock_top()) return 0;
    for (int i = 0; i < dock_count; i++) {
        if (x >= dock[i].x && x < dock[i].x + dock[i].w) {
            c26_cart_run(dock[i].name);
            return 1;
        }
    }
    return dock_count > 0;
}

/* ------------------------------------------------------------------ */
/* Surface primitives (draw into a process surface, clipped)           */

static void surface_pixel(uint32_t *surface, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < (int)C26_SCREEN_WIDTH &&
        y < (int)C26_SCREEN_HEIGHT) {
        surface[(unsigned int)y * C26_SCREEN_WIDTH + (unsigned int)x] = color;
    }
}

static void surface_fill(uint32_t *surface, int x, int y, int width,
                         int height, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)C26_SCREEN_WIDTH) x1 = C26_SCREEN_WIDTH;
    if (y1 > (int)C26_SCREEN_HEIGHT) y1 = C26_SCREEN_HEIGHT;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            surface[(unsigned int)py * C26_SCREEN_WIDTH + (unsigned int)px] =
                color;
        }
    }
}

static void surface_line(uint32_t *surface, int x0, int y0, int x1, int y1,
                         uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        surface_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
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

static void surface_text(uint32_t *surface, int x, int y, const char *text,
                         uint32_t fg, uint32_t bg, unsigned int scale)
{
    if (scale == 0) scale = 1;
    while (*text != '\0') {
        const uint8_t *glyph = c26_font_glyph(*text++);
        surface_fill(surface, x, y, (int)(6 * scale), (int)(8 * scale), bg);
        for (unsigned int column = 0; column < 5; column++) {
            for (unsigned int row = 0; row < 7; row++) {
                if ((glyph[column] & (1U << row)) != 0) {
                    surface_fill(surface, x + (int)(column * scale),
                                 y + (int)(row * scale), (int)scale,
                                 (int)scale, fg);
                }
            }
        }
        x += (int)(6 * scale);
    }
}

/* ------------------------------------------------------------------ */
/* Z-order, focus, and the compositor                                  */

static void z_remove(int job)
{
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == job) {
            for (int j = i + 1; j < z_count; j++) {
                z_order[j - 1] = z_order[j];
            }
            z_count--;
            return;
        }
    }
}

static void z_raise(int job)
{
    z_remove(job);
    z_order[z_count++] = job;
}

static void drain_typeahead(void)
{
    /* Stops as soon as a replayed line hands the queue to a new consumer
       (a BASIC RUN or another spawn), else pop/feed would cycle forever. */
    char ch;
    while (!c26_basic_running() && !c26_basic_queue_consumed_externally() &&
           c26_basic_key_pop(&ch)) {
        c26_basic_feed_char(ch);
    }
}

void c26_cart_focus_console(void)
{
    focused = -1;
    scene_dirty = 1;
    c26_basic_set_external_consumer(0);
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    drain_typeahead();
}

static void focus_proc(int index)
{
    focused = index;
    scene_dirty = 1;
    z_raise(index);
    c26_basic_set_external_consumer(1);
    c26_screen_set_mode(C26_SCREEN_CART);
}

void c26_cart_focus_next(void)
{
    int start = focused;
    for (int step = 1; step <= C26_NPROC; step++) {
        int candidate = start + step;
        if (candidate >= C26_NPROC) {
            c26_cart_focus_console();
            return;
        }
        if (procs[candidate].state == PROC_RUNNABLE) {
            focus_proc(candidate);
            return;
        }
    }
}

/* Titlebar / frame geometry, shared by the compositor and the hit tester so
   the affordances a user clicks are exactly the ones that were drawn. */
static int win_frame_w(const proc_t *p) { return p->win_w + 2 * BORDER; }
static int win_frame_h(const proc_t *p)
{
    return p->minimized ? TITLE_HEIGHT + BORDER
                        : p->win_h + TITLE_HEIGHT + BORDER;
}

/* Traffic-light dots on the LEFT of the title bar: close, minimize, zoom. */
static int dot_y(const proc_t *p) { return p->win_y + (TITLE_HEIGHT - DOT) / 2; }
static void close_box(const proc_t *p, int *bx, int *by)
{
    *bx = p->win_x + BORDER + DOT_LEFT;
    *by = dot_y(p);
}
static void min_box(const proc_t *p, int *bx, int *by)
{
    *bx = p->win_x + BORDER + DOT_LEFT + (DOT + DOT_GAP);
    *by = dot_y(p);
}
static void zoom_box(const proc_t *p, int *bx, int *by)
{
    *bx = p->win_x + BORDER + DOT_LEFT + 2 * (DOT + DOT_GAP);
    *by = dot_y(p);
}

static void grip_box(const proc_t *p, int *gx, int *gy)
{
    *gx = p->win_x + win_frame_w(p) - GRIP;
    *gy = p->win_y + win_frame_h(p) - GRIP;
}

static int in_box(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && x < bx + w && y >= by && y < by + h;
}

/* A round traffic-light dot: a filled disc approximated by stacked rows. */
static void draw_dot(int bx, int by, uint32_t color)
{
    static const int inset[DOT] = {5, 3, 2, 1, 1, 0, 0, 0, 0, 1, 1, 2, 3, 5};
    for (int r = 0; r < DOT; r++) {
        int in = inset[r];
        c26_fill_rect(bx + in, by + r, DOT - 2 * in, 1, color);
    }
}

static int str_pixels(const char *s) { int n = 0; while (s[n]) n++; return n * 12; }

/* Shared window chrome: drop shadow, frame, gradient-ish title bar, three
   macOS traffic lights on the left, a centred title. Content and the resize
   grip are drawn by the caller afterwards. */
static void draw_chrome(int wx, int wy, int cw, int frame_h, const char *title,
                        int focused, int show_close)
{
    int frame_w = cw + 2 * BORDER;
    uint32_t title_bg = focused ? 0x2b3475 : 0x1b2350;
    uint32_t edge = focused ? 0xd7ddffU : 0x4a5599U;
    static const int corner[4] = {3, 2, 1, 0}; /* rounded top corners */
    /* soft drop shadow down-right */
    c26_fill_rect(wx + 5, wy + 5, frame_w, frame_h, 0x070a1c);
    /* frame edges, with the top corners knocked in so they read as rounded
       (the pixels left unpainted keep the already-composited background). */
    c26_fill_rect(wx + 3, wy, frame_w - 6, 1, edge);            /* top */
    c26_fill_rect(wx, wy + 3, 1, frame_h - 3, edge);            /* left */
    c26_fill_rect(wx + frame_w - 1, wy + 3, 1, frame_h - 3, edge); /* right */
    c26_fill_rect(wx, wy + frame_h - 1, frame_w, 1, edge);      /* bottom */
    for (int r = 0; r < 4; r++) {
        c26_fill_rect(wx + corner[r], wy + r, 1, 1, edge);
        c26_fill_rect(wx + frame_w - 1 - corner[r], wy + r, 1, 1, edge);
    }
    /* title bar fill with matching rounded top */
    for (int r = 0; r < TITLE_HEIGHT - BORDER; r++) {
        int in = r < 4 ? corner[r] : 0;
        uint32_t c = r < 2 ? (focused ? 0x3c4699 : 0x232c63) : title_bg;
        c26_fill_rect(wx + BORDER + in, wy + BORDER + r, cw - 2 * in, 1, c);
    }
    int dy = wy + (TITLE_HEIGHT - DOT) / 2;
    int d0 = wx + BORDER + DOT_LEFT;
    draw_dot(d0, dy, focused ? (show_close ? COL_CLOSE : 0x6b6f8a) : 0x6b6f8a);
    draw_dot(d0 + (DOT + DOT_GAP), dy, focused ? COL_MIN : 0x6b6f8a);
    draw_dot(d0 + 2 * (DOT + DOT_GAP), dy, focused ? COL_ZOOM : 0x6b6f8a);
    int tx = wx + frame_w / 2 - str_pixels(title) / 2;
    int dots_right = d0 + 3 * (DOT + DOT_GAP) + 6;
    if (tx < dots_right) tx = dots_right;
    c26_draw_text(tx, wy + 6, title, focused ? 0xffffff : 0x9aa4d8, title_bg, 2);
}

static void draw_grip(int wx, int wy, int frame_w, int frame_h, int focused)
{
    int gx = wx + frame_w - GRIP;
    int gy = wy + frame_h - GRIP;
    uint32_t gc = focused ? 0xd7ddffU : 0x5560a0U;
    for (int i = 2; i < GRIP; i += 3) {
        c26_draw_line(gx + i, gy + GRIP - 1, gx + GRIP - 1, gy + i, gc);
    }
}

static void blit_window(const proc_t *process, const uint32_t *surface)
{
    uint32_t *pixels = c26_framebuffer_pixels();
    int content_x = process->win_x + BORDER;
    int content_y = process->win_y + TITLE_HEIGHT;
    int frame_w = win_frame_w(process);
    int frame_h = win_frame_h(process);
    int is_focused = process == &procs[focused >= 0 ? focused : 0] &&
                     focused >= 0;

    draw_chrome(process->win_x, process->win_y, process->win_w, frame_h,
                process->name, is_focused, 1);

    if (process->minimized) {
        return;
    }

    for (int row = 0; row < process->win_h; row++) {
        int dst_y = content_y + row;
        if (dst_y < 0 || dst_y >= (int)C26_SCREEN_HEIGHT) continue;
        for (int col = 0; col < process->win_w; col++) {
            int dst_x = content_x + col;
            if (dst_x < 0 || dst_x >= (int)C26_SCREEN_WIDTH) continue;
            pixels[(unsigned int)dst_y * C26_SCREEN_WIDTH +
                   (unsigned int)dst_x] =
                surface[(unsigned int)row * C26_SCREEN_WIDTH +
                        (unsigned int)col];
        }
    }

    draw_grip(process->win_x, process->win_y, frame_w, frame_h, is_focused);
}

void c26_compositor_mark_dirty(void)
{
    scene_dirty = 1;
}

/* Console window geometry (pixel content con_w x con_h, lazily initialised). */
static void con_ensure_size(void)
{
    if (con_w == 0) {
        con_w = c26_console_pixel_width();
        con_h = c26_console_pixel_height();
        int avail = (int)C26_SCREEN_HEIGHT - con_y - TITLE_HEIGHT - BORDER -
                    DOCK_H - 4;
        if (con_h > avail) con_h = avail;
    }
}
static int con_frame_w(void) { con_ensure_size(); return con_w + 2 * BORDER; }
static int con_frame_h(void)
{
    con_ensure_size();
    return con_min ? TITLE_HEIGHT + BORDER : con_h + TITLE_HEIGHT + BORDER;
}

/* A vertical gradient wallpaper — the C64-meets-macOS desktop. */
static void draw_wallpaper(void)
{
    const int bands = 48;
    int band_h = (int)C26_SCREEN_HEIGHT / bands + 1;
    /* top 0x1a2352 -> bottom 0x0b1130, a deep twilight blue. */
    for (int b = 0; b < bands; b++) {
        int r = 0x1a + (0x0b - 0x1a) * b / bands;
        int g = 0x23 + (0x11 - 0x23) * b / bands;
        int bl = 0x52 + (0x30 - 0x52) * b / bands;
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
        c26_fill_rect(0, b * band_h, (int)C26_SCREEN_WIDTH, band_h, c);
    }
}

/* Working dropdown menus. Each item either feeds BASIC a command, launches a
   cartridge, or shows an About line. */
typedef struct {
    const char *label;
    int kind; /* 0 feed BASIC, 1 launch cart, 2 about, 3 separator */
    const char *arg;
} menu_item_t;
static const menu_item_t items_c26[] = {
    {"ABOUT C26", 2, 0}, {"", 3, 0}, {"HELP", 0, "HELP\n"},
};
static const menu_item_t items_file[] = {
    {"NEW", 0, "NEW\n"},   {"LOAD DEMO", 0, "LOAD DEMO\n"},
    {"RUN", 0, "RUN\n"},   {"", 3, 0}, {"DIRECTORY", 0, "DIR\n"},
};
static const menu_item_t items_go[] = {
    {"CALC", 1, "CALC"},     {"CLOCK", 1, "CLOCK"}, {"PAINT", 1, "PAINT"},
    {"FILES", 1, "FILES"},   {"EDIT", 1, "EDIT"},   {"MONITOR", 1, "MONITOR"},
};
#define MENU_COUNT 3
static const struct {
    const char *title;
    const menu_item_t *items;
    int n;
    int x, w;
} menus[MENU_COUNT] = {
    {"C26", items_c26, 3, 4, 76},
    {"FILE", items_file, 5, 236, 56},
    {"GO", items_go, 6, 310, 44},
};
#define MENU_DROP_W 190
#define MENU_ROW_H 24
static int menu_open = -1;

static void draw_menu_bar(void)
{
    c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, MENU_H, 0x0d1233);
    c26_fill_rect(0, MENU_H - 1, (int)C26_SCREEN_WIDTH, 1, 0x3a4488);
    /* C26 badge (the apple-menu spot) + the clickable menu titles. */
    c26_fill_rect(10, 6, 16, 16, 0x68f0c0);
    c26_fill_rect(13, 9, 10, 10, 0x0d1233);
    for (int i = 0; i < MENU_COUNT; i++) {
        uint32_t bg = (menu_open == i) ? 0x35409a : 0x0d1233;
        if (menu_open == i) c26_fill_rect(menus[i].x, 2, menus[i].w, MENU_H - 3, bg);
        int tx = (i == 0) ? 34 : menus[i].x + 8;
        c26_draw_text(tx, 6, menus[i].title, 0xffffff, bg, 2);
    }
    const char *active = focused < 0 ? "BASIC" : procs[focused].name;
    c26_draw_text(100, 6, active, 0xffd34d, 0x0d1233, 2);
    uint64_t s = c26_rtc_seconds();
    unsigned int mm = (unsigned int)((s / 60) % 60);
    unsigned int hh = (unsigned int)((s / 3600) % 24);
    char clk[6] = {(char)('0' + hh / 10), (char)('0' + hh % 10), ':',
                   (char)('0' + mm / 10), (char)('0' + mm % 10), '\0'};
    c26_draw_text((int)C26_SCREEN_WIDTH - 70, 6, clk, 0xbac4ff, 0x0d1233, 2);

    if (menu_open < 0) return;
    /* The open dropdown, over everything. */
    int mx = menus[menu_open].x;
    int n = menus[menu_open].n;
    int mh = n * MENU_ROW_H + 8;
    c26_fill_rect(mx + 4, MENU_H + 4, MENU_DROP_W, mh, 0x070a1c); /* shadow */
    c26_fill_rect(mx, MENU_H, MENU_DROP_W, mh, 0x141a3a);
    c26_draw_rect(mx, MENU_H, MENU_DROP_W, mh, 0x4a5599);
    for (int i = 0; i < n; i++) {
        int ry = MENU_H + 4 + i * MENU_ROW_H;
        const menu_item_t *it = &menus[menu_open].items[i];
        if (it->kind == 3) {
            c26_fill_rect(mx + 6, ry + MENU_ROW_H / 2, MENU_DROP_W - 12, 1,
                          0x39427f);
        } else {
            c26_draw_text(mx + 14, ry + 4, it->label, 0xdfe4ff, 0x141a3a, 2);
        }
    }
}

static void run_menu_item(const menu_item_t *it)
{
    if (it->kind == 1) {
        c26_cart_run(it->arg);
    } else if (it->kind == 0) {
        c26_cart_focus_console();
        for (const char *p = it->arg; *p != '\0'; p++) c26_basic_feed_char(*p);
    } else if (it->kind == 2) {
        c26_cart_focus_console();
        c26_puts("C26 - the home computer where the C64 grew a desktop.\n");
    }
}

/* Route a click to the menus. Returns 1 if the menus consumed it. */
static int menu_click(int x, int y)
{
    if (y < MENU_H) {
        for (int i = 0; i < MENU_COUNT; i++) {
            if (x >= menus[i].x && x < menus[i].x + menus[i].w) {
                menu_open = (menu_open == i) ? -1 : i;
                scene_dirty = 1;
                return 1;
            }
        }
        menu_open = -1;
        scene_dirty = 1;
        return 1;
    }
    if (menu_open >= 0) {
        int mx = menus[menu_open].x;
        int n = menus[menu_open].n;
        int mh = n * MENU_ROW_H + 8;
        if (x >= mx && x < mx + MENU_DROP_W && y >= MENU_H && y < MENU_H + mh) {
            int idx = (y - MENU_H - 4) / MENU_ROW_H;
            if (idx >= 0 && idx < n && menus[menu_open].items[idx].kind != 3) {
                run_menu_item(&menus[menu_open].items[idx]);
            }
        }
        menu_open = -1;
        scene_dirty = 1;
        return 1; /* a click anywhere dismisses the open menu */
    }
    return 0;
}

/* The console as a first-class window: the same chrome as an app (minimize,
   zoom, resize, move), only the red close is inert — it is the shell. */
static void draw_console_window(void)
{
    con_ensure_size();
    int is_focused = focused < 0;
    draw_chrome(con_x, con_y, con_w, con_frame_h(), "BASIC", is_focused, 0);
    if (con_min) return;
    c26_console_blit(con_x + BORDER, con_y + TITLE_HEIGHT, con_w, con_h);
    draw_grip(con_x, con_y, con_frame_w(), con_frame_h(), is_focused);
}

void c26_compositor_flush(void)
{
    c26_screen_mode_t mode = c26_screen_mode();
    if (mode != C26_SCREEN_CONSOLE && mode != C26_SCREEN_CART) {
        return;
    }
    int damage = scene_dirty || c26_console_dirty();
    for (int i = 0; i < z_count; i++) {
        damage |= procs[z_order[i]].surface_damaged;
    }
    if (!damage) {
        return;
    }
    scene_dirty = 0;
    /* The unified desktop: wallpaper, the console window at the bottom of the
       z-order, app windows over it, the dock, the top menu bar, the pointer. */
    draw_wallpaper();
    draw_console_window();
    for (int i = 0; i < z_count; i++) {
        proc_t *process = &procs[z_order[i]];
        process->surface_damaged = 0;
        blit_window(process, proc_surface[z_order[i]]);
    }
    if (dock_count > 0) {
        dock_draw();
    }
    draw_menu_bar();
    c26_desktop_draw_pointer();
    c26_framebuffer_present();
}

/* Window-manager input, called by the desktop with pointer coordinates. */
/* Drop any in-progress title-bar drag or corner resize. Called when input
   leaves the compositor (e.g. Esc to the desktop) so a held button doesn't
   leave a window stuck to the pointer after the release is delivered elsewhere. */
void c26_wm_cancel_drag(void)
{
    dragging = -1;
    resizing = -1;
}

/* Green "zoom" button: fill the desktop (below the menu bar, above the dock),
   or restore the pre-zoom geometry. */
static void toggle_zoom(proc_t *p)
{
    if (p->zoomed) {
        p->win_x = p->save_x;
        p->win_y = p->save_y;
        p->win_w = p->save_w;
        p->win_h = p->save_h;
        p->zoomed = 0;
    } else {
        p->save_x = p->win_x;
        p->save_y = p->win_y;
        p->save_w = p->win_w;
        p->save_h = p->win_h;
        p->win_x = BORDER;
        p->win_y = MENU_H;
        p->win_w = (int)C26_SCREEN_WIDTH - 2 * BORDER;
        p->win_h = (int)C26_SCREEN_HEIGHT - MENU_H - TITLE_HEIGHT - BORDER -
                   DOCK_H;
        p->minimized = 0;
        p->zoomed = 1;
    }
    p->surface_damaged = 1;
    scene_dirty = 1;
}

/* Green zoom for the console window: fill the desktop, or restore. */
static void toggle_con_zoom(void)
{
    con_ensure_size();
    int maxw = c26_console_pixel_width();
    int maxh = c26_console_pixel_height();
    if (con_zoom) {
        con_x = con_sx;
        con_y = con_sy;
        con_w = con_sw;
        con_h = con_sh;
        con_zoom = 0;
    } else {
        con_sx = con_x;
        con_sy = con_y;
        con_sw = con_w;
        con_sh = con_h;
        con_x = 4;
        con_y = MENU_H + 4;
        con_w = (int)C26_SCREEN_WIDTH - 8;
        con_h = (int)C26_SCREEN_HEIGHT - MENU_H - TITLE_HEIGHT - BORDER - DOCK_H - 8;
        if (con_w > maxw) con_w = maxw;
        if (con_h > maxh) con_h = maxh;
        con_min = 0;
        con_zoom = 1;
    }
    scene_dirty = 1;
}

int c26_wm_click(int x, int y, int pressed)
{
    if (!pressed) {
        dragging = -1;
        resizing = -1;
        con_dragging = 0;
        con_resizing = 0;
        return 0;
    }
    if (menu_click(x, y)) {
        return 1;
    }
    if (dock_click(x, y)) {
        return 1;
    }
    for (int i = z_count - 1; i >= 0; i--) {
        int job = z_order[i];
        proc_t *process = &procs[job];
        if (!in_box(x, y, process->win_x, process->win_y, win_frame_w(process),
                    win_frame_h(process))) {
            continue;
        }
        focus_proc(job);
        int bx, by;
        close_box(process, &bx, &by);
        if (in_box(x, y, bx, by, DOT, DOT)) {
            c26_cart_kill(job);
            return 1;
        }
        min_box(process, &bx, &by);
        if (in_box(x, y, bx, by, DOT, DOT)) {
            process->minimized = !process->minimized;
            scene_dirty = 1;
            return 1;
        }
        zoom_box(process, &bx, &by);
        if (in_box(x, y, bx, by, DOT, DOT)) {
            toggle_zoom(process);
            return 1;
        }
        if (!process->minimized) {
            int gx, gy;
            grip_box(process, &gx, &gy);
            if (in_box(x, y, gx, gy, GRIP, GRIP)) {
                resizing = job;
                return 1;
            }
        }
        if (y < process->win_y + TITLE_HEIGHT) {
            dragging = job;
            drag_dx = x - process->win_x;
            drag_dy = y - process->win_y;
        }
        return 1;
    }
    /* The console window (the shell) sits under every app window. It has the
       same controls as an app: amber minimizes, green zooms, the grip resizes,
       the title bar moves it; only the red close is inert (it is the shell). */
    if (in_box(x, y, con_x, con_y, con_frame_w(), con_frame_h())) {
        c26_cart_focus_console();
        int dy = con_y + (TITLE_HEIGHT - DOT) / 2;
        int d0 = con_x + BORDER + DOT_LEFT;
        if (in_box(x, y, d0 + (DOT + DOT_GAP), dy, DOT, DOT)) {
            con_min = !con_min;
            scene_dirty = 1;
            return 1;
        }
        if (in_box(x, y, d0 + 2 * (DOT + DOT_GAP), dy, DOT, DOT)) {
            toggle_con_zoom();
            return 1;
        }
        if (!con_min) {
            int gx = con_x + con_frame_w() - GRIP;
            int gy = con_y + con_frame_h() - GRIP;
            if (in_box(x, y, gx, gy, GRIP, GRIP)) {
                con_resizing = 1;
                return 1;
            }
        }
        if (y < con_y + TITLE_HEIGHT) {
            con_dragging = 1;
            con_drag_dx = x - con_x;
            con_drag_dy = y - con_y;
        }
        return 1;
    }
    if (focused >= 0) {
        c26_cart_focus_console();
    }
    return 0;
}

/* A window's content is [0,w) x [0,h) of its full-screen surface, so resizing
   is just clamping win_w/win_h — no reallocation. Apps poll window_size() and
   relayout on their next frame. */
static void clamp_window_size(proc_t *process)
{
    int max_w = (int)C26_SCREEN_WIDTH - 2 * BORDER;
    int max_h = (int)C26_SCREEN_HEIGHT - TITLE_HEIGHT - BORDER;
    if (process->win_w < MIN_WIN_W) process->win_w = MIN_WIN_W;
    if (process->win_h < MIN_WIN_H) process->win_h = MIN_WIN_H;
    if (process->win_w > max_w) process->win_w = max_w;
    if (process->win_h > max_h) process->win_h = max_h;
}

void c26_wm_pointer_moved(int x, int y)
{
    if (con_resizing) {
        con_ensure_size();
        con_w = x - (con_x + BORDER);
        con_h = y - (con_y + TITLE_HEIGHT);
        int maxw = c26_console_pixel_width();
        int maxh = c26_console_pixel_height();
        if (con_w < 30 * 12) con_w = 30 * 12;
        if (con_h < 8 * 20) con_h = 8 * 20;
        if (con_w > maxw) con_w = maxw;
        if (con_h > maxh) con_h = maxh;
        con_zoom = 0;
        scene_dirty = 1;
        return;
    }
    if (con_dragging) {
        con_x = x - con_drag_dx;
        con_y = y - con_drag_dy;
        if (con_x < -con_frame_w() + 60) con_x = -con_frame_w() + 60;
        if (con_y < MENU_H) con_y = MENU_H;
        if (con_x > (int)C26_SCREEN_WIDTH - 60)
            con_x = (int)C26_SCREEN_WIDTH - 60;
        if (con_y > (int)C26_SCREEN_HEIGHT - TITLE_HEIGHT)
            con_y = (int)C26_SCREEN_HEIGHT - TITLE_HEIGHT;
        scene_dirty = 1;
        return;
    }
    if (resizing >= 0 && procs[resizing].state == PROC_RUNNABLE) {
        proc_t *process = &procs[resizing];
        process->win_w = x - (process->win_x + BORDER);
        process->win_h = y - (process->win_y + TITLE_HEIGHT);
        clamp_window_size(process);
    } else if (dragging >= 0 && procs[dragging].state == PROC_RUNNABLE) {
        proc_t *process = &procs[dragging];
        process->win_x = x - drag_dx;
        process->win_y = y - drag_dy;
        if (process->win_x < -process->win_w + 40)
            process->win_x = -process->win_w + 40;
        if (process->win_y < MENU_H) process->win_y = MENU_H;
        if (process->win_x > (int)C26_SCREEN_WIDTH - 40)
            process->win_x = (int)C26_SCREEN_WIDTH - 40;
        if (process->win_y > (int)C26_SCREEN_HEIGHT - TITLE_HEIGHT)
            process->win_y = (int)C26_SCREEN_HEIGHT - TITLE_HEIGHT;
    }
    scene_dirty = 1; /* the pointer moved over the unified desktop; repaint */
}

/* ------------------------------------------------------------------ */
/* Syscalls                                                            */

static uintptr_t user_ptr(uint64_t va, uint64_t size, int write)
{
    if (current < 0) {
        return 0;
    }
    return c26_vm_translate(&procs[current].space, va, size, write);
}

static const char *user_string(uint64_t va)
{
    uintptr_t base = user_ptr(va, 1, 0);
    if (base == 0) {
        return 0;
    }
    for (uint64_t length = 1; length <= 4096; length++) {
        if (user_ptr(va, length, 0) == 0) {
            return 0;
        }
        if (*(const char *)(base + length - 1) == '\0') {
            return (const char *)base;
        }
    }
    return 0;
}

static uint64_t read_csr_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static uint64_t read_csr_mtval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
}

static long do_syscall(c26_user_frame_t *frame)
{
    proc_t *process = &procs[current];
    uint64_t number = C26_FRAME_X(frame, 17);
    uint64_t a0 = C26_FRAME_X(frame, 10);
    uint64_t a1 = C26_FRAME_X(frame, 11);
    uint64_t a2 = C26_FRAME_X(frame, 12);
    uint64_t a3 = C26_FRAME_X(frame, 13);
    uint64_t a4 = C26_FRAME_X(frame, 14);
    uint64_t a5 = C26_FRAME_X(frame, 15);

    switch (number) {
    case C26_SYS_EXIT:
        c26_user_terminate((long)(int64_t)a0);
    case C26_SYS_PUTS: {
        const char *text = user_string(a0);
        if (text == 0) break;
        c26_puts(text);
        return 0;
    }
    case C26_SYS_PUTC:
        c26_putc((char)a0);
        return 0;
    case C26_SYS_PUT_INT:
        c26_put_int((int64_t)a0);
        return 0;
    case C26_SYS_GETCHAR: {
        char ch;
        if (current != focused) return -1;
        if (!c26_basic_key_pop(&ch)) return -1;
        /* Cartridges use the uppercase-key convention (e.g. paint's Q/C). */
        if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
        return (long)(uint8_t)ch;
    }
    case C26_SYS_MOUSE: {
        int x;
        int y;
        int buttons;
        c26_desktop_mouse(&x, &y, &buttons);
        /* Window-local coordinates; buttons only while focused and not
           when the pointer is over the window chrome. */
        x -= process->win_x + BORDER;
        y -= process->win_y + TITLE_HEIGHT;
        if (current != focused || x < 0 || y < 0 || x >= process->win_w ||
            y >= process->win_h) {
            buttons = 0;
        }
        uintptr_t px = a0 != 0 ? user_ptr(a0, 4, 1) : 1;
        uintptr_t py = a1 != 0 ? user_ptr(a1, 4, 1) : 1;
        uintptr_t pb = a2 != 0 ? user_ptr(a2, 4, 1) : 1;
        if (px == 0 || py == 0 || pb == 0) break;
        if (a0 != 0) *(int *)px = x;
        if (a1 != 0) *(int *)py = y;
        if (a2 != 0) *(int *)pb = buttons;
        return 0;
    }
    case C26_SYS_STOP_REQUESTED:
        return process->kill_pending || c26_basic_break_requested();
    case C26_SYS_TICKS:
        return (long)c26_interrupt_ticks();
    case C26_SYS_YIELD:
        c26_user_terminate(EXIT_SLICE);
    case C26_SYS_FRAMEBUFFER:
        return (long)(uintptr_t)proc_surface[current];
    case C26_SYS_PIXEL:
        surface_pixel(proc_surface[current], (int)a0, (int)a1, (uint32_t)a2);
        return 0;
    case C26_SYS_FILL_RECT:
        surface_fill(proc_surface[current], (int)a0, (int)a1, (int)a2,
                     (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_DRAW_RECT:
        surface_fill(proc_surface[current], (int)a0, (int)a1, (int)a2, 1,
                     (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0, (int)a1 + (int)a3 - 1,
                     (int)a2, 1, (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0, (int)a1, 1, (int)a3,
                     (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0 + (int)a2 - 1, (int)a1,
                     1, (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_LINE:
        surface_line(proc_surface[current], (int)a0, (int)a1, (int)a2,
                     (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_TEXT: {
        const char *text = user_string(a2);
        if (text == 0) break;
        surface_text(proc_surface[current], (int)a0, (int)a1, text,
                     (uint32_t)a3, (uint32_t)a4, (unsigned int)a5);
        return 0;
    }
    case C26_SYS_PRESENT:
        process->surface_damaged = 1;
        return 0;
    case C26_SYS_VOICE_START:
        if ((int64_t)a1 < 0 || a1 > 3) return 0;
        return c26_audio_voice_start((unsigned int)a0, (c26_waveform_t)a1,
                                     (uint32_t)a2, (uint8_t)a3, (uint8_t)a4);
    case C26_SYS_VOICE_STOP:
        c26_audio_voice_stop((unsigned int)a0);
        return 0;
    case C26_SYS_FS_SAVE: {
        const char *name = user_string(a0);
        uintptr_t data = user_ptr(a1, a2, 0);
        if (name == 0 || data == 0) break;
        return c26_fs_save(name, (const void *)data, a2);
    }
    case C26_SYS_FS_LOAD: {
        const char *name = user_string(a0);
        uintptr_t data = user_ptr(a1, a2, 1);
        if (name == 0 || data == 0) break;
        size_t size = 0;
        long ok = c26_fs_load(name, (void *)data, a2, &size);
        if (a3 != 0) {
            uintptr_t out = user_ptr(a3, 8, 1);
            if (out == 0) break;
            *(uint64_t *)out = size;
        }
        return ok;
    }
    case C26_SYS_FS_DELETE: {
        const char *name = user_string(a0);
        if (name == 0) break;
        return c26_fs_delete(name);
    }
    case C26_SYS_FS_COUNT:
        return (long)c26_fs_count();
    case C26_SYS_FS_ENTRY: {
        const char *name;
        uint32_t size;
        if (!c26_fs_entry(a0, &name, &size)) return 0;
        char *scratch = proc_scratch[current];
        size_t length = 0;
        while (name[length] != '\0' && length < 4095) {
            scratch[length] = name[length];
            length++;
        }
        scratch[length] = '\0';
        if (a1 != 0) {
            uintptr_t out = user_ptr(a1, 8, 1);
            if (out == 0) break;
            *(uint64_t *)out = (uint64_t)(uintptr_t)scratch;
        }
        if (a2 != 0) {
            uintptr_t out = user_ptr(a2, 4, 1);
            if (out == 0) break;
            *(uint32_t *)out = size;
        }
        return 1;
    }
    case C26_SYS_DEV_READ8: {
        uintptr_t out = user_ptr(a1, 1, 1);
        if (out == 0) break;
        uint8_t value = 0;
        long ok = c26_device_read8((uint16_t)a0, &value);
        *(uint8_t *)out = value;
        return ok;
    }
    case C26_SYS_DEV_WRITE8:
        return c26_device_write8((uint16_t)a0, (uint8_t)a1);
    case C26_SYS_WINDOW_SIZE: {
        uintptr_t pw = a0 != 0 ? user_ptr(a0, 4, 1) : 1;
        uintptr_t ph = a1 != 0 ? user_ptr(a1, 4, 1) : 1;
        if (pw == 0 || ph == 0) break;
        if (a0 != 0) *(uint32_t *)pw = (uint32_t)process->win_w;
        if (a1 != 0) *(uint32_t *)ph = (uint32_t)process->win_h;
        return 0;
    }
    case C26_SYS_CLIP_SET: {
        unsigned int len = (unsigned int)a1;
        if (len > CLIPBOARD_MAX) len = CLIPBOARD_MAX;
        uintptr_t p = user_ptr(a0, len == 0 ? 1 : len, 0);
        if (p == 0) break;
        c26_clipboard_set((const char *)p, len);
        return (long)len;
    }
    case C26_SYS_CLIP_GET: {
        unsigned int cap = (unsigned int)a1;
        uintptr_t p = user_ptr(a0, cap == 0 ? 1 : cap, 1);
        if (p == 0) break;
        return (long)c26_clipboard_get((char *)p, cap);
    }
    case C26_SYS_RTC_SECONDS:
        return (long)c26_rtc_seconds();
    case C26_SYS_TCP_CONNECT:
        return c26_tcp_connect((uint32_t)a0, (uint16_t)a1);
    case C26_SYS_TCP_SEND: {
        unsigned int len = (unsigned int)a1;
        uintptr_t p = user_ptr(a0, len == 0 ? 1 : len, 0);
        if (p == 0) break;
        return c26_tcp_send((const void *)p, len);
    }
    case C26_SYS_TCP_RECV: {
        unsigned int cap = (unsigned int)a1;
        uintptr_t p = user_ptr(a0, cap == 0 ? 1 : cap, 1);
        if (p == 0) break;
        return c26_tcp_recv((void *)p, cap);
    }
    case C26_SYS_TCP_CLOSE:
        c26_tcp_close();
        return 0;
    case C26_SYS_TCP_STATE:
        return c26_tcp_state();
    case C26_SYS_DNS_RESOLVE: {
        const char *name = user_string(a0);
        uintptr_t out = user_ptr(a1, 4, 1);
        if (name == 0 || out == 0) break;
        uint32_t ip = 0;
        int ok = c26_dns_resolve(name, &ip);
        *(uint32_t *)out = ip;
        return ok;
    }
    case C26_SYS_SEND: {
        int target = (int)(int64_t)a0;
        if (target < 0 || target >= C26_NPROC ||
            procs[target].state != PROC_RUNNABLE || a2 > MESSAGE_MAX) {
            return 0;
        }
        uintptr_t data = user_ptr(a1, a2 == 0 ? 1 : a2, 0);
        if (data == 0) break;
        proc_t *peer = &procs[target];
        if (peer->mail_head - peer->mail_tail >= MAILBOX_SLOTS) {
            return 0;
        }
        message_t *message = &peer->mail[peer->mail_head % MAILBOX_SLOTS];
        message->from = (uint8_t)current;
        message->length = (uint16_t)a2;
        memcpy(message->data, (const void *)data, a2);
        peer->mail_head++;
        return 1;
    }
    case C26_SYS_SPAWN: {
        const char *name = user_string(a0);
        if (name == 0) break;
        return c26_cart_run(name);
    }
    case C26_SYS_UDP_BIND:
        return c26_udp_bind((uint16_t)a0);
    case C26_SYS_UDP_SEND: {
        uintptr_t data = user_ptr(a3, a4 == 0 ? 1 : a4, 0);
        if (data == 0) break;
        return c26_udp_send((uint32_t)a0, (uint16_t)a1, (uint16_t)a2,
                            (const void *)data, a4);
    }
    case C26_SYS_UDP_RECV: {
        uintptr_t data = user_ptr(a3, a4 == 0 ? 1 : a4, 1);
        if (data == 0) break;
        uint32_t from_ip = 0;
        uint16_t from_port = 0;
        int received = c26_udp_recv((uint16_t)a0, &from_ip, &from_port,
                                    (void *)data, a4);
        if (a1 != 0) {
            uintptr_t out = user_ptr(a1, 4, 1);
            if (out == 0) break;
            *(uint32_t *)out = from_ip;
        }
        if (a2 != 0) {
            uintptr_t out = user_ptr(a2, 2, 1);
            if (out == 0) break;
            *(uint16_t *)out = from_port;
        }
        return received;
    }
    case C26_SYS_RECV: {
        if (process->mail_tail == process->mail_head) {
            return -1;
        }
        message_t *message = &process->mail[process->mail_tail % MAILBOX_SLOTS];
        uint64_t copy = message->length < a2 ? message->length : a2;
        if (copy != 0) {
            uintptr_t data = user_ptr(a1, copy, 1);
            if (data == 0) break;
            memcpy((void *)data, message->data, copy);
        }
        if (a0 != 0) {
            uintptr_t from = user_ptr(a0, 4, 1);
            if (from == 0) break;
            *(int *)from = message->from;
        }
        process->mail_tail++;
        return (long)copy;
    }
    default:
        break;
    }
    c26_puts("CART FAULT bad syscall or pointer\n");
    c26_user_terminate(-1);
}

void c26_trap_handler_user(c26_user_frame_t *frame)
{
    uint64_t cause = read_csr_mcause();
    if ((cause & (1ULL << 63)) != 0) {
        cause &= ~(1ULL << 63);
        if (cause == 7) {
            c26_timer_interrupt();
            slice_ticks_left--;
        } else if (cause == 11) {
            c26_external_interrupt();
        }
        c26_io_pump();
        if (c26_basic_break_requested() && focused >= 0) {
            procs[focused].kill_pending = 1;
            c26_basic_clear_break();
        }
        if (current >= 0 && procs[current].kill_pending) {
            c26_user_terminate(-3);
        }
        if (slice_ticks_left <= 0) {
            c26_user_terminate(EXIT_SLICE);
        }
        return;
    }
    if (cause == 8) { /* ecall from U-mode */
        frame->mepc += 4;
        C26_FRAME_X(frame, 10) = (uint64_t)do_syscall(frame);
        return;
    }
    c26_puts("CART FAULT cause=");
    c26_put_hex(cause);
    c26_puts(" addr=");
    c26_put_hex(read_csr_mtval());
    c26_putc('\n');
    c26_user_terminate(-1);
}

/* ------------------------------------------------------------------ */
/* Process lifecycle and scheduling                                    */

static void finalize(int index, long code)
{
    procs[index].state = PROC_FREE;
    procs[index].kill_pending = 0;
    z_remove(index);
    scene_dirty = 1;
    if (dragging == index) {
        dragging = -1;
    }
    if (resizing == index) {
        resizing = -1;
    }
    procs[index].minimized = 0;
    if (code == -3) {
        c26_puts("CART KILLED\n");
    }
    c26_puts("CART EXIT ");
    c26_put_int(code);
    c26_putc('\n');
    if (focused == index) {
        c26_cart_focus_console();
    }
}

int c26_cart_run(const char *name)
{
    int slot = -1;
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        c26_puts("Error: all job slots are busy (see JOBS)\n");
        return -1;
    }
    uint8_t *base = (uint8_t *)(uintptr_t)(C26_CART_BASE +
                                           (uint64_t)slot * C26_CART_MAX_BYTES);
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

    proc_t *process = &procs[slot];
    c26_vm_init(&process->space);
    if (!c26_vm_map(&process->space, C26_CART_BASE, (uint64_t)(uintptr_t)base,
                    C26_CART_MAX_BYTES, 1, 1) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_stack[slot],
                    (uint64_t)(uintptr_t)proc_stack[slot], USER_STACK_BYTES,
                    1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_scratch[slot],
                    (uint64_t)(uintptr_t)proc_scratch[slot], 4096, 1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_surface[slot],
                    (uint64_t)(uintptr_t)proc_surface[slot],
                    SURFACE_PIXELS * 4, 1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)c26_user_stub_base,
                    (uint64_t)(uintptr_t)c26_user_stub_base, 4096, 0, 1)) {
        c26_puts("Error: cartridge address space setup failed\n");
        return -1;
    }

    size_t name_length = 0;
    while (name[name_length] != '\0' && name_length < C26_FS_NAME_MAX) {
        process->name[name_length] = name[name_length];
        name_length++;
    }
    process->name[name_length] = '\0';
    memset(proc_surface[slot], 0, SURFACE_PIXELS * 4);
    memset(&process->frame, 0, sizeof(process->frame));
    process->frame.mepc = C26_CART_BASE + header->entry_offset;
    C26_FRAME_X(&process->frame, 1) =
        (uint64_t)(uintptr_t)c26_user_stub_exit;
    C26_FRAME_X(&process->frame, 2) =
        (uint64_t)(uintptr_t)proc_stack[slot] + USER_STACK_BYTES;
    C26_FRAME_X(&process->frame, 10) = (uint64_t)(uintptr_t)c26_user_api;
    process->kill_pending = 0;
    process->surface_damaged = 1;
    process->win_w = WINDOW_DEFAULT_W;
    process->win_h = WINDOW_DEFAULT_H;
    process->win_x = 80 + slot * 96;
    process->win_y = 60 + slot * 68;
    process->minimized = 0;
    process->zoomed = 0;
    process->mail_head = 0;
    process->mail_tail = 0;
    process->state = PROC_RUNNABLE;

    c26_puts("CART START ");
    c26_puts(name);
    c26_puts(" AS JOB ");
    c26_put_uint((uint64_t)slot);
    c26_putc('\n');
    focus_proc(slot);
    return slot;
}

int c26_cart_any_runnable(void)
{
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_RUNNABLE) {
            return 1;
        }
    }
    return 0;
}

int c26_cart_move_window(int job, int x, int y)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    procs[job].win_x = x;
    procs[job].win_y = y;
    scene_dirty = 1;
    return 1;
}

int c26_cart_focus(int job)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    focus_proc(job);
    return 1;
}

int c26_cart_resize_window(int job, int w, int h)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    procs[job].win_w = w;
    procs[job].win_h = h;
    clamp_window_size(&procs[job]);
    procs[job].minimized = 0;
    procs[job].surface_damaged = 1;
    scene_dirty = 1;
    return 1;
}

int c26_cart_set_minimized(int job, int minimized)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    procs[job].minimized = minimized ? 1 : 0;
    scene_dirty = 1;
    return 1;
}

int c26_cart_send(int job, const void *data, size_t size)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE ||
        size > MESSAGE_MAX) {
        return 0;
    }
    proc_t *peer = &procs[job];
    if (peer->mail_head - peer->mail_tail >= MAILBOX_SLOTS) {
        return 0;
    }
    message_t *message = &peer->mail[peer->mail_head % MAILBOX_SLOTS];
    message->from = 254; /* the console */
    message->length = (uint16_t)size;
    memcpy(message->data, data, size);
    peer->mail_head++;
    return 1;
}

int c26_cart_kill(int job)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    procs[job].kill_pending = 1;
    return 1;
}

void c26_cart_list_jobs(void)
{
    int any = 0;
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state != PROC_RUNNABLE) continue;
        any = 1;
        c26_puts("  JOB ");
        c26_put_uint((uint64_t)i);
        c26_puts("  ");
        c26_puts(procs[i].name);
        c26_puts(i == focused ? "  [FOCUSED]\n" : "\n");
    }
    if (!any) {
        c26_puts("  (NO JOBS)\n");
    }
}

void c26_cart_schedule(void)
{
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_RUNNABLE && procs[i].kill_pending) {
            finalize(i, -3);
        }
    }
    int next = -1;
    for (int step = 0; step < C26_NPROC; step++) {
        int candidate = (rr_cursor + step) % C26_NPROC;
        if (procs[candidate].state == PROC_RUNNABLE) {
            next = candidate;
            break;
        }
    }
    if (next < 0) {
        return;
    }
    rr_cursor = (next + 1) % C26_NPROC;
    current = next;
    slice_ticks_left = SLICE_TICKS;
    c26_vm_activate(&procs[next].space);
    long code = c26_user_enter(&procs[next].frame);
    current = -1;
    if (code != EXIT_SLICE) {
        finalize(next, code);
    }
}
