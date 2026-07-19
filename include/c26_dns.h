#ifndef C26_DNS_H
#define C26_DNS_H

/* Pure DNS A-record codec: message build and parse with no I/O, so the same
 * code runs in the kernel resolver (src/net.c) and in a host unit test
 * (tests/test_dns.c). The network transport lives in net.c; everything here
 * is byte-shuffling that a host can verify deterministically. */

#include <stddef.h>
#include <stdint.h>

static inline uint16_t c26_dns_be16(const uint8_t *b)
{
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline void c26_dns_put16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
}

static inline uint32_t c26_dns_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* Accept a bare dotted-quad ("10.0.2.2") without a query. Returns 1 on match,
 * filling *out_ip in host byte order. */
static inline int c26_dns_parse_literal(const char *name, uint32_t *out_ip)
{
    uint32_t ip = 0;
    int octet = 0, digits = 0, parts = 0;
    for (const char *c = name;; c++) {
        if (*c >= '0' && *c <= '9') {
            octet = octet * 10 + (*c - '0');
            if (octet > 255 || ++digits > 3) return 0;
        } else if (*c == '.' || *c == 0) {
            if (digits == 0) return 0;
            ip = (ip << 8) | (uint32_t)octet;
            parts++;
            octet = 0;
            digits = 0;
            if (*c == 0) break;
        } else {
            return 0;
        }
    }
    if (parts != 4) return 0;
    *out_ip = ip;
    return 1;
}

/* Encode host name as DNS length-prefixed labels + QTYPE A / QCLASS IN.
 * Returns the query length in bytes, or 0 on a malformed name / short buffer. */
static inline int c26_dns_build_query(uint8_t *buf, size_t cap,
                                      const char *name, uint16_t id)
{
    if (cap < 12 + 6) return 0;
    c26_dns_put16(buf, id);
    c26_dns_put16(buf + 2, 0x0100); /* standard query, recursion desired */
    c26_dns_put16(buf + 4, 1);      /* qdcount */
    c26_dns_put16(buf + 6, 0);
    c26_dns_put16(buf + 8, 0);
    c26_dns_put16(buf + 10, 0);
    size_t p = 12;
    size_t label_len_at = p++;
    unsigned label_len = 0;
    for (const char *c = name;; c++) {
        if (*c == '.' || *c == 0) {
            if (label_len == 0 || label_len > 63) return 0;
            buf[label_len_at] = (uint8_t)label_len;
            if (*c == 0) break;
            if (p + 6 >= cap) return 0;
            label_len_at = p++;
            label_len = 0;
        } else {
            if (p + 6 >= cap) return 0;
            buf[p++] = (uint8_t)*c;
            label_len++;
        }
    }
    buf[p++] = 0;                     /* root label */
    c26_dns_put16(buf + p, 1); p += 2; /* QTYPE  A  */
    c26_dns_put16(buf + p, 1); p += 2; /* QCLASS IN */
    return (int)p;
}

/* Skip a DNS name (labels or a 0xC0 compression pointer) starting at *p. */
static inline void c26_dns_skip_name(const uint8_t *msg, size_t len, size_t *p)
{
    while (*p < len) {
        uint8_t l = msg[*p];
        if (l == 0) { *p += 1; return; }
        if ((l & 0xc0) == 0xc0) { *p += 2; return; }
        *p += (size_t)l + 1;
    }
}

/* Parse a response matching id; return 1 and the first A record's IP. */
static inline int c26_dns_parse_answer(const uint8_t *msg, size_t len,
                                       uint16_t id, uint32_t *out_ip)
{
    if (len < 12 || c26_dns_be16(msg) != id) return 0;
    uint16_t flags = c26_dns_be16(msg + 2);
    if ((flags & 0x8000) == 0 || (flags & 0x000f) != 0) return 0;
    uint16_t qd = c26_dns_be16(msg + 4);
    uint16_t an = c26_dns_be16(msg + 6);
    size_t p = 12;
    for (uint16_t i = 0; i < qd; i++) {
        c26_dns_skip_name(msg, len, &p);
        p += 4; /* qtype + qclass */
    }
    for (uint16_t i = 0; i < an; i++) {
        c26_dns_skip_name(msg, len, &p);
        if (p + 10 > len) return 0;
        uint16_t type = c26_dns_be16(msg + p);
        uint16_t rdlength = c26_dns_be16(msg + p + 8);
        p += 10;
        if (type == 1 && rdlength == 4 && p + 4 <= len) {
            *out_ip = c26_dns_be32(msg + p);
            return 1;
        }
        p += rdlength;
    }
    return 0;
}

#endif
