#include "c26.h"
#include "c26_graphics.h"

#define DEPTH_FAR 0x7fffffff

static int32_t depth_buffer[C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT]
    __attribute__((aligned(64)));

static int64_t edge(c26_gl_vertex_t a, c26_gl_vertex_t b, int x, int y)
{
    return (int64_t)(x - a.x) * (b.y - a.y) -
           (int64_t)(y - a.y) * (b.x - a.x);
}

void c26_gl_clear(uint32_t color)
{
    c26_fill_rect(0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT, color);
    c26_gl_clear_depth();
}

void c26_gl_clear_depth(void)
{
    for (unsigned int i = 0; i < C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT; i++) {
        depth_buffer[i] = DEPTH_FAR;
    }
}

static uint32_t interpolate_color(c26_gl_vertex_t a, c26_gl_vertex_t b,
                                  c26_gl_vertex_t c, int64_t wa, int64_t wb,
                                  int64_t wc, int64_t area)
{
    uint32_t ar = (a.color >> 16) & 0xff;
    uint32_t ag = (a.color >> 8) & 0xff;
    uint32_t ab = a.color & 0xff;
    uint32_t br = (b.color >> 16) & 0xff;
    uint32_t bg = (b.color >> 8) & 0xff;
    uint32_t bb = b.color & 0xff;
    uint32_t cr = (c.color >> 16) & 0xff;
    uint32_t cg = (c.color >> 8) & 0xff;
    uint32_t cb = c.color & 0xff;
    uint32_t red = (uint32_t)((wa * ar + wb * br + wc * cr) / area);
    uint32_t green = (uint32_t)((wa * ag + wb * bg + wc * cg) / area);
    uint32_t blue = (uint32_t)((wa * ab + wb * bb + wc * cb) / area);
    return (red << 16) | (green << 8) | blue;
}

void c26_gl_draw_triangle(c26_gl_vertex_t a, c26_gl_vertex_t b,
                          c26_gl_vertex_t c)
{
    int64_t area = edge(a, b, c.x, c.y);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        c26_gl_vertex_t swap = b;
        b = c;
        c = swap;
        area = -area;
    }
    int min_x = a.x;
    int max_x = a.x;
    int min_y = a.y;
    int max_y = a.y;
    if (b.x < min_x) min_x = b.x;
    if (c.x < min_x) min_x = c.x;
    if (b.x > max_x) max_x = b.x;
    if (c.x > max_x) max_x = c.x;
    if (b.y < min_y) min_y = b.y;
    if (c.y < min_y) min_y = c.y;
    if (b.y > max_y) max_y = b.y;
    if (c.y > max_y) max_y = c.y;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)C26_SCREEN_WIDTH) max_x = C26_SCREEN_WIDTH - 1;
    if (max_y >= (int)C26_SCREEN_HEIGHT) max_y = C26_SCREEN_HEIGHT - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int64_t wa = edge(b, c, x, y);
            int64_t wb = edge(c, a, x, y);
            int64_t wc = edge(a, b, x, y);
            if (wa < 0 || wb < 0 || wc < 0) {
                continue;
            }
            int32_t z = (int32_t)((wa * a.z + wb * b.z + wc * c.z) / area);
            unsigned int index = (unsigned int)y * C26_SCREEN_WIDTH +
                                 (unsigned int)x;
            if (z < depth_buffer[index]) {
                depth_buffer[index] = z;
                c26_draw_pixel(x, y,
                    interpolate_color(a, b, c, wa, wb, wc, area));
            }
        }
    }
}

void c26_gl_demo_cube(int origin_x, int origin_y, unsigned int scale)
{
    static const int8_t model[8][3] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},
    };
    static const uint8_t triangles[12][3] = {
        {0,1,2},{0,2,3},{4,6,5},{4,7,6},
        {0,4,5},{0,5,1},{3,2,6},{3,6,7},
        {1,5,6},{1,6,2},{0,3,7},{0,7,4},
    };
    static const uint32_t colors[8] = {
        0x2458d3,0x39c6ff,0x68f0c0,0x3d80ff,
        0xff5ca8,0xffad45,0xffe568,0xaa66ff,
    };
    c26_gl_vertex_t vertices[8];
    for (unsigned int i = 0; i < 8; i++) {
        int x = model[i][0];
        int y = model[i][1];
        int z = model[i][2];
        vertices[i].x = origin_x + (x - z) * (int)scale / 2;
        vertices[i].y = origin_y + (x + z) * (int)scale / 4 -
                        y * (int)scale / 2;
        vertices[i].z = 1000 + z * 120 - y * 20;
        vertices[i].color = colors[i];
    }
    for (unsigned int i = 0; i < 12; i++) {
        c26_gl_draw_triangle(vertices[triangles[i][0]],
                             vertices[triangles[i][1]],
                             vertices[triangles[i][2]]);
    }
}

static unsigned int integer_sqrt(unsigned int value)
{
    unsigned int result = 0;
    unsigned int bit = 1U << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

void c26_raytrace_demo(int x, int y, int width, int height)
{
    for (int py = 0; py < height; py++) {
        for (int px = 0; px < width; px++) {
            int sx = (px - width / 2) * 128 / width;
            int sy = (py - height / 2) * 96 / height;
            int dx1 = sx + 30;
            int dy1 = sy;
            int dx2 = sx - 35;
            int dy2 = sy + 8;
            int inside1 = 44 * 44 - dx1 * dx1 - dy1 * dy1;
            int inside2 = 30 * 30 - dx2 * dx2 - dy2 * dy2;
            uint32_t color = 0x101a38 + (uint32_t)(py * 24 / height) * 0x010101;
            if (inside1 >= 0) {
                unsigned int normal = integer_sqrt((unsigned int)inside1);
                unsigned int light = 45 + normal * 210 / 44;
                color = (light << 16) | ((light / 3) << 8) | (light / 2);
            }
            if (inside2 >= 0) {
                unsigned int normal = integer_sqrt((unsigned int)inside2);
                unsigned int light = 45 + normal * 210 / 30;
                color = ((light / 4) << 16) | ((light / 2) << 8) | light;
            }
            c26_draw_pixel(x + px, y + py, color);
        }
    }
}

void c26_graphics_render_demo(void)
{
    c26_fill_rect(326, 154, 298, 300, 0x10162f);
    c26_draw_rect(326, 154, 298, 300, 0x7185ff);
    c26_fill_rect(327, 155, 296, 24, 0x303c82);
    c26_draw_text(338, 162, "3D AND RAY LAB", 0xffffff, 0x303c82, 1);
    c26_gl_clear_depth();
    c26_gl_demo_cube(475, 230, 78);
    c26_draw_text(342, 276, "SOFTWARE GL TRIANGLES", 0x68f0c0, 0x10162f, 1);
    c26_raytrace_demo(344, 298, 260, 126);
    c26_draw_text(342, 435, "CPU RAY TRACED SPHERES", 0xffad45, 0x10162f, 1);
}

void c26_graphics_demo(void)
{
    c26_graphics_render_demo();
    c26_framebuffer_present();
    c26_puts("OPENGL-STYLE SDK: z-buffered triangle rasterizer online\n");
    c26_puts("RAY TRACER: two shaded spheres rendered\n");
}
