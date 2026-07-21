/* FETCH: an HTTP-get client. Type a host (a name or a dotted-quad) and Enter;
 * FETCH resolves it (DNS), opens TCP to port 80, sends a GET, and shows the
 * response. It closes "every network capability reachable from a cartridge" —
 * TCP and DNS reach the app layer through the ABI v5 syscalls, not just UDP.
 * Talks to the machine only through the c26_api_t. */

#include "c26_api.h"

#define RESP_MAX 4096
#define HOST_MAX 64

static uint32_t width;
static uint32_t height;
static char host[HOST_MAX];
static int host_len;
static char resp[RESP_MAX + 1];
static int resp_len;

static void put_uint(const c26_api_t *api, uint32_t v)
{
    api->put_int((int64_t)v);
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

static void draw(const c26_api_t *api, const char *status)
{
    vgrad(api, 0, 0, (int)width, (int)height, 0x141a36, 0x0a0e20);
    /* glossy title bar */
    vgrad(api, 0, 0, (int)width, 24, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x4a56b4);   /* bright top highlight */
    api->fill_rect(0, 23, (int)width, 1, 0x0a0e20);  /* dark seam shadow */
    label_fg(api, 6, 4, "FETCH", 0xffffff, 0x30397e, 2);
    label_fg(api, (int)width - 120, 8, "ENTER GO Q QUIT", 0x9df6ff, 0x30397e, 1);

    /* recessed URL/status bar */
    vgrad(api, 4, 28, (int)width - 8, 22, 0x0c1226, 0x060a18);
    api->fill_rect(4, 28, (int)width - 8, 1, 0x04060e);
    label_fg(api, 8, 32, "http://", 0x6570bd, 0x0c1226, 2);
    host[host_len] = '\0';
    label_fg(api, 90, 32, host, 0x68f0c0, 0x0c1226, 2);
    label_fg(api, 8, 54, status, 0xffd34d, 0x141a36, 1);

    /* recessed "screen" behind the response body */
    vgrad(api, 4, 64, (int)width - 8, (int)height - 68, 0x0c1226, 0x060a18);
    api->fill_rect(4, 64, (int)width - 8, 1, 0x04060e);

    /* Response body: draw as wrapped lines below the input. */
    int y = 70;
    int col = 0;
    int line_w = ((int)width - 16) / 6;
    char line[128];
    int n = 0;
    for (int i = 0; i < resp_len && y < (int)height - 12; i++) {
        char c = resp[i];
        if (c == '\n' || col >= line_w || n >= 126) {
            line[n] = '\0';
            label_fg(api, 8, y, line, 0xbac4ff, 0x0c1226, 1);
            y += 12;
            n = 0;
            col = 0;
            if (c == '\n') continue;
        }
        if (c == '\r') continue;
        line[n++] = (c >= 32 && c <= 126) ? c : '.';
        col++;
    }
    if (n > 0 && y < (int)height - 12) {
        line[n] = '\0';
        label_fg(api, 8, y, line, 0xbac4ff, 0x0c1226, 1);
    }
}

/* Resolve, connect, GET, and drain the response into resp[]. */
static void do_fetch(const c26_api_t *api)
{
    host[host_len] = '\0';
    resp_len = 0;
    api->puts("FETCH GET ");
    api->puts(host);
    api->putc('\n');

    uint32_t ip = 0;
    if (!api->dns_resolve(host, &ip)) {
        api->puts("FETCH RESOLVE FAILED\n");
        draw(api, "RESOLVE FAILED");
        api->present();
        return;
    }
    api->puts("FETCH RESOLVED ");
    put_uint(api, (ip >> 24) & 0xff); api->putc('.');
    put_uint(api, (ip >> 16) & 0xff); api->putc('.');
    put_uint(api, (ip >> 8) & 0xff); api->putc('.');
    put_uint(api, ip & 0xff); api->putc('\n');

    if (!api->tcp_connect(ip, 80)) {
        api->puts("FETCH OFFLINE\n");
        return;
    }
    uint64_t deadline = api->ticks() + 400;
    while (api->ticks() < deadline && api->tcp_state() == 1) {
        api->idle();
    }
    if (api->tcp_state() != 2) {
        api->puts("FETCH FAILED\n");
        draw(api, "CONNECT FAILED");
        api->present();
        return;
    }
    api->puts("FETCH CONNECTED\n");

    /* Minimal HTTP/1.0 request; Connection: close lets the server EOF the body. */
    char req[128];
    int r = 0;
    const char *p = "GET / HTTP/1.0\r\nHost: ";
    while (*p) req[r++] = *p++;
    for (int i = 0; i < host_len; i++) req[r++] = host[i];
    p = "\r\nConnection: close\r\n\r\n";
    while (*p) req[r++] = *p++;
    api->tcp_send(req, (size_t)r);

    deadline = api->ticks() + 400;
    for (;;) {
        char chunk[512];
        int got = api->tcp_recv(chunk, sizeof(chunk));
        if (got > 0) {
            for (int i = 0; i < got && resp_len < RESP_MAX; i++) {
                resp[resp_len++] = chunk[i];
            }
            api->puts("FETCH RX ");
            put_uint(api, (uint32_t)got);
            api->putc('\n');
            deadline = api->ticks() + 400;
        } else if (got < 0) {
            break; /* EOF: server closed */
        }
        if (api->ticks() >= deadline) break;
        api->idle();
    }
    api->tcp_close();
    resp[resp_len] = '\0';
    api->puts(resp); /* echo the response so the fetch is checkable */
    api->putc('\n');
    api->puts("FETCH DONE ");
    put_uint(api, (uint32_t)resp_len);
    api->putc('\n');
    draw(api, "DONE");
    api->present();
}

int app_main(const c26_api_t *api)
{
    if (api->version < 5) {
        api->puts("FETCH NEEDS ABI 5\n");
        return 1;
    }
    api->window_size(&width, &height);
    api->puts("FETCH CART ONLINE\n");
    draw(api, "TYPE A HOST, THEN ENTER");
    api->present();

    int dirty = 0;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;
        if (ch == '\n' || ch == '\r') {
            if (host_len > 0) do_fetch(api);
        } else if ((ch == '\b' || ch == 0x7f) && host_len > 0) {
            host_len--;
            dirty = 1;
        } else if (ch > 32 && ch <= 126 && host_len < HOST_MAX - 1) {
            host[host_len++] = (char)ch;
            dirty = 1;
        }
        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 2) {
            last_present = now;
            dirty = 0;
            draw(api, "TYPE A HOST, THEN ENTER");
            api->present();
        }
        api->idle();
    }
    api->puts("FETCH CART EXIT\n");
    return 0;
}
