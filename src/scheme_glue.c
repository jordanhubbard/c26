#include "c26.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_scheme.h"

/* Kernel glue for c26 Scheme: the platform hooks that make the interpreter's
 * primitives drive the real machine, and a paren-balancing REPL feed that
 * BASIC hands lines to when the user types SCHEME. */

static const uint32_t scheme_palette[16] = {
    0x000000, 0xffffff, 0x883932, 0x67b6bd, 0x8b3f96, 0x55a049,
    0x40318d, 0xbfce72, 0x8b5429, 0x574200, 0xb86962, 0x505050,
    0x787878, 0x94e089, 0x7869c4, 0x9f9f9f,
};
static uint32_t scheme_color = 0xffffff;

static uint32_t resolve_color(int c)
{
    if (c >= 0 && c < 16) return scheme_palette[c];
    return (uint32_t)c & 0xffffff;
}

static void plat_color(int c) { scheme_color = resolve_color(c); }
static void plat_plot(int x, int y) { c26_draw_pixel(x, y, scheme_color); }
static void plat_line(int x0, int y0, int x1, int y1)
{
    c26_draw_line(x0, y0, x1, y1, scheme_color);
}
static void plat_rect(int x, int y, int w, int h, int fill)
{
    if (fill) c26_fill_rect(x, y, w, h, scheme_color);
    else c26_draw_rect(x, y, w, h, scheme_color);
}
static void plat_text(int x, int y, const char *s)
{
    c26_draw_text(x, y, s, scheme_color, 0x000000, 2);
}
static void plat_cls(void)
{
    c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, (int)C26_SCREEN_HEIGHT, 0);
}
static void plat_present(void) { c26_framebuffer_present(); }
static void plat_sound(int voice, int freq)
{
    if (freq <= 0) {
        c26_audio_voice_stop((unsigned int)voice);
    } else {
        c26_audio_voice_start((unsigned int)voice, C26_WAVE_SQUARE,
                              (uint32_t)freq, 200, 128);
    }
}
static int plat_fs_save(const char *name, const char *data, int size)
{
    return c26_fs_save(name, data, (size_t)size);
}
static int plat_fs_load(const char *name, char *buf, int cap)
{
    size_t size = 0;
    if (!c26_fs_load(name, buf, (size_t)cap, &size)) return -1;
    return (int)size;
}
static void plat_screen(int mode)
{
    if (mode == 1) {
        c26_screen_set_mode(C26_SCREEN_GFX);
        c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, (int)C26_SCREEN_HEIGHT, 0);
        c26_framebuffer_present();
    } else {
        c26_screen_set_mode(C26_SCREEN_CONSOLE);
    }
}

static const scm_platform_t kernel_platform = {
    plat_color, plat_plot,  plat_line,  plat_rect,    plat_text,
    plat_cls,   plat_present, plat_sound, plat_fs_save, plat_fs_load,
    plat_screen,
};

/* Called by BASIC when the user leaves Scheme, so any graphics screen the
 * REPL left up returns to the text console. */
void c26_scheme_leave(void)
{
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
}

static int scheme_ready;
static char scheme_src[8192];
static unsigned int scheme_src_len;
static int scheme_paren_depth;

void c26_scheme_enter(void)
{
    if (!scheme_ready) {
        scm_set_output(c26_puts);
        scm_init();
        scm_set_platform(&kernel_platform);
        scheme_ready = 1;
    }
    scheme_src_len = 0;
    scheme_paren_depth = 0;
    c26_puts("C26 SCHEME - LISP REPL, PRIMITIVES DRIVE THE MACHINE\n");
    c26_puts("TYPE exit TO RETURN TO BASIC\n");
}

/* Update paren depth for a line, ignoring parens inside "strings" and after
 * ; comments. Returns nothing; mutates scheme_paren_depth. */
static void scan_parens(const char *line)
{
    int in_string = 0;
    for (const char *p = line; *p != '\0'; p++) {
        if (in_string) {
            if (*p == '"') in_string = 0;
            continue;
        }
        if (*p == ';') break;
        if (*p == '"') in_string = 1;
        else if (*p == '(') scheme_paren_depth++;
        else if (*p == ')' && scheme_paren_depth > 0) scheme_paren_depth--;
    }
}

/* Feed one console line to the REPL. Returns 1 when a complete form was
 * evaluated (caller shows a fresh prompt), 0 when more input is needed
 * (unbalanced parens -> continuation prompt). */
int c26_scheme_feed(const char *line)
{
    size_t i = 0;
    while (line[i] != '\0' && scheme_src_len + 1 < sizeof(scheme_src)) {
        scheme_src[scheme_src_len++] = line[i++];
    }
    if (scheme_src_len + 1 < sizeof(scheme_src)) {
        scheme_src[scheme_src_len++] = '\n';
    }
    scheme_src[scheme_src_len] = '\0';
    scan_parens(line);

    /* Keep buffering while parens are open and there is room. */
    if (scheme_paren_depth > 0 && scheme_src_len + 1 < sizeof(scheme_src)) {
        return 0;
    }

    scm_eval_string(scheme_src, 1);
    scheme_src_len = 0;
    scheme_paren_depth = 0;
    return 1;
}
