/* Host unit tests for the pure DNS A-record codec (include/c26_dns.h). The
 * query builder and response parser are byte-shuffling with no I/O, so the
 * host verifies them deterministically — the recursive resolution path that
 * needs a live DNS server is exercised separately (RESOLVE in the machine). */

#include "c26_dns.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int cond, const char *label)
{
    if (!cond) {
        failures++;
        printf("FAIL %s\n", label);
    }
}

int main(void)
{
    uint32_t ip = 0;

    /* Dotted-quad literals resolve without a query. */
    expect(c26_dns_parse_literal("10.0.2.2", &ip) && ip == 0x0a000202u,
           "literal 10.0.2.2");
    expect(c26_dns_parse_literal("255.255.255.255", &ip) && ip == 0xffffffffu,
           "literal broadcast");
    expect(!c26_dns_parse_literal("10.0.2", &ip), "literal too few octets");
    expect(!c26_dns_parse_literal("10.0.2.256", &ip), "literal octet overflow");
    expect(!c26_dns_parse_literal("example.com", &ip), "literal rejects name");
    expect(!c26_dns_parse_literal("1.2.3.4.5", &ip), "literal too many octets");

    /* Query encoding: header, labels, QTYPE/QCLASS. */
    uint8_t q[64];
    int qlen = c26_dns_build_query(q, sizeof q, "www.c26.dev", 0xBEEF);
    /* 12 header + (1+3)+(1+3)+(1+3) labels + 1 root + 4 qtype/qclass = 29 */
    expect(qlen == 29, "query length");
    expect(c26_dns_be16(q) == 0xBEEF, "query id");
    expect(c26_dns_be16(q + 2) == 0x0100, "query RD flag");
    expect(c26_dns_be16(q + 4) == 1, "query qdcount");
    expect(q[12] == 3 && memcmp(q + 13, "www", 3) == 0, "label www");
    expect(q[16] == 3 && memcmp(q + 17, "c26", 3) == 0, "label c26");
    expect(q[20] == 3 && memcmp(q + 21, "dev", 3) == 0, "label dev");
    expect(q[24] == 0, "root label");
    expect(c26_dns_be16(q + 25) == 1 && c26_dns_be16(q + 27) == 1,
           "qtype/qclass A/IN");

    expect(c26_dns_build_query(q, sizeof q, "", 1) == 0, "empty name rejected");
    expect(c26_dns_build_query(q, sizeof q, "a..b", 1) == 0,
           "empty label rejected");

    /* Response parsing with a compression pointer to the question name. */
    uint8_t r[] = {
        0xBE, 0xEF,             /* id */
        0x81, 0x80,             /* response, RD+RA, rcode 0 */
        0x00, 0x01,             /* qdcount 1 */
        0x00, 0x01,             /* ancount 1 */
        0x00, 0x00, 0x00, 0x00, /* ns/ar */
        0x03, 'w', 'w', 'w', 0x03, 'c', '2', '6', 0x03, 'd', 'e', 'v', 0x00,
        0x00, 0x01, 0x00, 0x01, /* qtype A / qclass IN */
        0xC0, 0x0C,             /* name: pointer to offset 12 */
        0x00, 0x01, 0x00, 0x01, /* type A, class IN */
        0x00, 0x00, 0x01, 0x2C, /* ttl 300 */
        0x00, 0x04,             /* rdlength 4 */
        0x5D, 0xB8, 0xD8, 0x22, /* 93.184.216.34 */
    };
    ip = 0;
    expect(c26_dns_parse_answer(r, sizeof r, 0xBEEF, &ip) &&
               ip == 0x5DB8D822u,
           "parse A record via compression pointer");
    expect(!c26_dns_parse_answer(r, sizeof r, 0x1234, &ip), "id mismatch");

    /* rcode NXDOMAIN (3) must be rejected. */
    uint8_t nx[] = {0xBE, 0xEF, 0x81, 0x83, 0, 0, 0, 0, 0, 0, 0, 0};
    expect(!c26_dns_parse_answer(nx, sizeof nx, 0xBEEF, &ip), "NXDOMAIN rejected");

    /* A CNAME answer (type 5) is skipped; the following A record wins. */
    uint8_t chain[] = {
        0xBE, 0xEF, 0x81, 0x80, 0x00, 0x01, 0x00, 0x02, 0, 0, 0, 0,
        0x03, 'w', 'w', 'w', 0x00, 0x00, 0x01, 0x00, 0x01, /* question */
        0xC0, 0x0C, 0x00, 0x05, 0x00, 0x01, 0, 0, 0, 0x3C, /* CNAME rr */
        0x00, 0x02, 0xC0, 0x0C,                            /* rdata: ptr */
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 0x3C, /* A rr */
        0x00, 0x04, 0x08, 0x08, 0x08, 0x08,                /* 8.8.8.8 */
    };
    ip = 0;
    expect(c26_dns_parse_answer(chain, sizeof chain, 0xBEEF, &ip) &&
               ip == 0x08080808u,
           "skip CNAME, take A record");

    if (failures == 0) {
        printf("test_dns: all DNS codec assertions passed\n");
    }
    return failures ? 1 : 0;
}
