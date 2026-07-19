#include "c26.h"
#include "c26_dns.h"
#include "c26_net.h"
#include "c26_virtio.h"

#define VIRTIO_DEVICE_NET 1U
#define VIRTIO_NET_F_MAC (1U << 5)
#define NET_HDR_BYTES 12U
#define FRAME_MAX 1526U
#define RX_BUFFERS 8U
#define BINDING_COUNT 4U
#define QUEUE_RING 4U

#define ETH_TYPE_IP 0x0800U
#define ETH_TYPE_ARP 0x0806U
#define IP_PROTO_ICMP 1U
#define IP_PROTO_UDP 17U

#define OUR_IP 0x0a00020fU     /* 10.0.2.15 (QEMU user net default) */
#define GATEWAY_IP 0x0a000202U /* 10.0.2.2 */
#define DNS_SERVER 0x0a000203U /* 10.0.2.3 (QEMU user net DNS) */

typedef struct {
    c26_virtq_desc_t descriptors[C26_VIRTQ_SIZE];
    c26_virtq_avail_t available;
    c26_virtq_used_t used;
} net_queue_memory_t;

typedef struct {
    uint32_t ip;
    uint16_t port;
    uint16_t length;
    uint8_t data[C26_UDP_DATA_MAX];
} udp_datagram_t;

typedef struct {
    uint16_t port; /* 0 = free */
    unsigned int head;
    unsigned int tail;
    udp_datagram_t ring[QUEUE_RING];
} udp_binding_t;

static c26_virtio_device_t net_device;
static c26_virtq_t rx_queue;
static c26_virtq_t tx_queue;
static net_queue_memory_t rx_memory __attribute__((aligned(4096)));
static net_queue_memory_t tx_memory __attribute__((aligned(4096)));
static uint8_t rx_buffer[RX_BUFFERS][NET_HDR_BYTES + FRAME_MAX]
    __attribute__((aligned(64)));
static uint8_t tx_buffer[NET_HDR_BYTES + FRAME_MAX]
    __attribute__((aligned(64)));
static int tx_pending;

static uint8_t our_mac[6];
static uint8_t gateway_mac[6];
static int have_gateway_mac;
static int online;
static udp_binding_t bindings[BINDING_COUNT];

static uint16_t load16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

static uint32_t load32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void store16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void store32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint16_t checksum16(const uint8_t *data, size_t size)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < size; i += 2) {
        sum += load16(data + i);
    }
    if ((size & 1) != 0) {
        sum += (uint32_t)data[size - 1] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* ------------------------------------------------------------------ */
/* Frame transmit                                                      */

static void transmit(const uint8_t *frame, size_t size)
{
    uint32_t id;
    while (c26_virtq_pop(&tx_queue, &id, 0)) {
        tx_pending = 0;
    }
    if (tx_pending) {
        for (uint32_t spin = 0; spin < 10000000U && tx_pending; spin++) {
            if (c26_virtq_pop(&tx_queue, &id, 0)) {
                tx_pending = 0;
            }
        }
        if (tx_pending) {
            return; /* device wedged; drop rather than hang */
        }
    }
    memset(tx_buffer, 0, NET_HDR_BYTES);
    memcpy(tx_buffer + NET_HDR_BYTES, frame, size);
    tx_memory.descriptors[0] = (c26_virtq_desc_t){
        (uint64_t)(uintptr_t)tx_buffer, (uint32_t)(NET_HDR_BYTES + size), 0,
        0};
    tx_pending = 1;
    c26_virtq_submit(&tx_queue, 0);
}

static void send_arp_request(void)
{
    uint8_t frame[42];
    memset(frame, 0xff, 6);
    memcpy(frame + 6, our_mac, 6);
    store16(frame + 12, ETH_TYPE_ARP);
    store16(frame + 14, 1);      /* ethernet */
    store16(frame + 16, ETH_TYPE_IP);
    frame[18] = 6;
    frame[19] = 4;
    store16(frame + 20, 1);      /* request */
    memcpy(frame + 22, our_mac, 6);
    store32(frame + 28, OUR_IP);
    memset(frame + 32, 0, 6);
    store32(frame + 38, GATEWAY_IP);
    transmit(frame, sizeof(frame));
}

static void send_udp(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                     const void *data, size_t size)
{
    if (size > C26_UDP_DATA_MAX || !have_gateway_mac) {
        if (!have_gateway_mac) send_arp_request();
        return;
    }
    uint8_t frame[14 + 20 + 8 + C26_UDP_DATA_MAX];
    memcpy(frame, gateway_mac, 6);
    memcpy(frame + 6, our_mac, 6);
    store16(frame + 12, ETH_TYPE_IP);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0;
    store16(ip + 2, (uint16_t)(20 + 8 + size));
    store16(ip + 4, 0);
    store16(ip + 6, 0x4000); /* don't fragment */
    ip[8] = 64;
    ip[9] = IP_PROTO_UDP;
    store16(ip + 10, 0);
    store32(ip + 12, OUR_IP);
    store32(ip + 16, dst_ip);
    store16(ip + 10, checksum16(ip, 20));
    uint8_t *udp = ip + 20;
    store16(udp, src_port);
    store16(udp + 2, dst_port);
    store16(udp + 4, (uint16_t)(8 + size));
    store16(udp + 6, 0); /* checksum optional in IPv4 */
    memcpy(udp + 8, data, size);
    transmit(frame, 14 + 20 + 8 + size);
}

/* ------------------------------------------------------------------ */
/* Frame receive                                                       */

static udp_binding_t *find_binding(uint16_t port)
{
    for (unsigned int i = 0; i < BINDING_COUNT; i++) {
        if (bindings[i].port == port) {
            return &bindings[i];
        }
    }
    return 0;
}

static void handle_arp(const uint8_t *frame, size_t size)
{
    if (size < 42 || load16(frame + 16) != ETH_TYPE_IP) {
        return;
    }
    uint16_t operation = load16(frame + 20);
    uint32_t sender_ip = load32(frame + 28);
    if (sender_ip == GATEWAY_IP) {
        memcpy(gateway_mac, frame + 22, 6);
        have_gateway_mac = 1;
    }
    if (operation == 1 && load32(frame + 38) == OUR_IP) {
        uint8_t reply[42];
        memcpy(reply, frame + 6, 6);
        memcpy(reply + 6, our_mac, 6);
        store16(reply + 12, ETH_TYPE_ARP);
        store16(reply + 14, 1);
        store16(reply + 16, ETH_TYPE_IP);
        reply[18] = 6;
        reply[19] = 4;
        store16(reply + 20, 2); /* reply */
        memcpy(reply + 22, our_mac, 6);
        store32(reply + 28, OUR_IP);
        memcpy(reply + 32, frame + 22, 6);
        store32(reply + 38, sender_ip);
        transmit(reply, sizeof(reply));
    }
}

static void handle_icmp(const uint8_t *frame, const uint8_t *ip, size_t size)
{
    size_t header = (size_t)(ip[0] & 0xf) * 4;
    const uint8_t *icmp = ip + header;
    size_t icmp_size = load16(ip + 2) - header;
    if (size < 14 + header + icmp_size || icmp_size < 8 || icmp[0] != 8) {
        return;
    }
    uint8_t reply[14 + 20 + 576];
    if (icmp_size > 576) return;
    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, our_mac, 6);
    store16(reply + 12, ETH_TYPE_IP);
    uint8_t *rip = reply + 14;
    memcpy(rip, ip, 20);
    store32(rip + 12, OUR_IP);
    store32(rip + 16, load32(ip + 12));
    store16(rip + 2, (uint16_t)(20 + icmp_size));
    rip[8] = 64;
    store16(rip + 10, 0);
    store16(rip + 10, checksum16(rip, 20));
    uint8_t *ricmp = rip + 20;
    memcpy(ricmp, icmp, icmp_size);
    ricmp[0] = 0; /* echo reply */
    store16(ricmp + 2, 0);
    store16(ricmp + 2, checksum16(ricmp, icmp_size));
    transmit(reply, 14 + 20 + icmp_size);
}

static void handle_udp(const uint8_t *frame, const uint8_t *ip, size_t size)
{
    size_t header = (size_t)(ip[0] & 0xf) * 4;
    const uint8_t *udp = ip + header;
    if (size < 14 + header + 8) {
        return;
    }
    uint16_t dst_port = load16(udp + 2);
    uint16_t src_port = load16(udp);
    uint16_t udp_length = load16(udp + 4);
    if (udp_length < 8 || 14 + header + udp_length > size + 4) {
        return;
    }
    size_t payload = udp_length - 8;
    if (payload > C26_UDP_DATA_MAX) {
        return;
    }
    uint32_t src_ip = load32(ip + 12);
    /* Learn the gateway's MAC from traffic it forwards to us. */
    if (!have_gateway_mac) {
        memcpy(gateway_mac, frame + 6, 6);
        have_gateway_mac = 1;
    }
    if (dst_port == C26_NET_ECHO_PORT) {
        send_udp(src_ip, src_port, C26_NET_ECHO_PORT, udp + 8, payload);
        return;
    }
    udp_binding_t *binding = find_binding(dst_port);
    if (binding == 0 ||
        binding->head - binding->tail >= QUEUE_RING) {
        return;
    }
    udp_datagram_t *slot = &binding->ring[binding->head % QUEUE_RING];
    slot->ip = src_ip;
    slot->port = src_port;
    slot->length = (uint16_t)payload;
    memcpy(slot->data, udp + 8, payload);
    binding->head++;
}

/* ------------------------------------------------------------------ */
/* TCP: a deliberately small single-connection client                  */

#define IP_PROTO_TCP 6U
#define TCP_FIN 0x01U
#define TCP_SYN 0x02U
#define TCP_RST 0x04U
#define TCP_PSH 0x08U
#define TCP_ACK 0x10U
#define TCP_WINDOW 2048U
#define TCP_DATA_MAX 512U
#define TCP_RX_MAX 2048U
#define TCP_REXMIT_TICKS 40U /* ~0.4s at the 100 Hz timer */
#define TCP_SYN_RETRIES 5

enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT, /* peer sent FIN; we may still drain rxbuf */
    TCP_FIN_WAIT,   /* we sent FIN, awaiting peer's */
    TCP_FAILED,
};

static struct {
    int state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t rxbuf[TCP_RX_MAX];
    unsigned int rx_len;
    int syn_pending;
    int syn_retries;
    uint64_t rexmit_deadline;
} tcp;

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, const uint8_t *seg,
                             uint16_t seg_len)
{
    uint32_t sum = 0;
    sum += (src >> 16) & 0xffff;
    sum += src & 0xffff;
    sum += (dst >> 16) & 0xffff;
    sum += dst & 0xffff;
    sum += IP_PROTO_TCP;
    sum += seg_len;
    for (uint16_t i = 0; i + 1 < seg_len; i += 2) sum += load16(seg + i);
    if (seg_len & 1) sum += (uint32_t)seg[seg_len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void send_tcp(uint8_t flags, uint32_t seq, const uint8_t *data,
                     uint16_t len)
{
    if (!have_gateway_mac) {
        send_arp_request();
        return;
    }
    uint8_t frame[14 + 20 + 20 + TCP_DATA_MAX];
    memcpy(frame, gateway_mac, 6);
    memcpy(frame + 6, our_mac, 6);
    store16(frame + 12, ETH_TYPE_IP);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0;
    store16(ip + 2, (uint16_t)(20 + 20 + len));
    store16(ip + 4, 0);
    store16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IP_PROTO_TCP;
    store16(ip + 10, 0);
    store32(ip + 12, OUR_IP);
    store32(ip + 16, tcp.remote_ip);
    store16(ip + 10, checksum16(ip, 20));
    uint8_t *t = ip + 20;
    store16(t, tcp.local_port);
    store16(t + 2, tcp.remote_port);
    store32(t + 4, seq);
    store32(t + 8, (flags & TCP_ACK) ? tcp.rcv_nxt : 0);
    t[12] = 5 << 4; /* data offset: 5 words, no options */
    t[13] = flags;
    store16(t + 14, TCP_WINDOW);
    store16(t + 16, 0);
    store16(t + 18, 0);
    if (len) memcpy(t + 20, data, len);
    store16(t + 16, tcp_checksum(OUR_IP, tcp.remote_ip, t, (uint16_t)(20 + len)));
    transmit(frame, 14 + 20 + 20 + len);
}

static void handle_tcp(const uint8_t *ip, size_t size)
{
    size_t ihl = (size_t)(ip[0] & 0xf) * 4;
    const uint8_t *t = ip + ihl;
    if (size < 14 + ihl + 20 || tcp.state == TCP_CLOSED) {
        return;
    }
    if (load32(ip + 12) != tcp.remote_ip ||
        load16(t) != tcp.remote_port ||
        load16(t + 2) != tcp.local_port) {
        return;
    }
    uint32_t seg_seq = load32(t + 4);
    uint32_t seg_ack = load32(t + 8);
    uint8_t flags = t[13];
    size_t data_off = (size_t)(t[12] >> 4) * 4;
    size_t total = load16(ip + 2);
    size_t payload_len = total > ihl + data_off ? total - ihl - data_off : 0;
    const uint8_t *payload = t + data_off;

    if (flags & TCP_RST) {
        tcp.state = TCP_FAILED;
        return;
    }
    if (tcp.state == TCP_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
            seg_ack == tcp.snd_nxt) {
            tcp.rcv_nxt = seg_seq + 1;
            tcp.snd_una = seg_ack;
            tcp.state = TCP_ESTABLISHED;
            tcp.syn_pending = 0;
            send_tcp(TCP_ACK, tcp.snd_nxt, 0, 0);
        }
        return;
    }
    if ((flags & TCP_ACK) && seg_ack - tcp.snd_una <= tcp.snd_nxt - tcp.snd_una) {
        tcp.snd_una = seg_ack;
    }
    if (payload_len > 0 && seg_seq == tcp.rcv_nxt) {
        unsigned int space = TCP_RX_MAX - tcp.rx_len;
        unsigned int copy = payload_len < space ? (unsigned int)payload_len
                                                : space;
        memcpy(tcp.rxbuf + tcp.rx_len, payload, copy);
        tcp.rx_len += copy;
        tcp.rcv_nxt += copy; /* ack only what we accepted */
        send_tcp(TCP_ACK, tcp.snd_nxt, 0, 0);
    } else if (payload_len > 0) {
        send_tcp(TCP_ACK, tcp.snd_nxt, 0, 0); /* duplicate/out of order */
    }
    if ((flags & TCP_FIN) && seg_seq + payload_len == tcp.rcv_nxt) {
        tcp.rcv_nxt += 1;
        send_tcp(TCP_ACK, tcp.snd_nxt, 0, 0);
        tcp.state = tcp.state == TCP_FIN_WAIT ? TCP_CLOSED : TCP_CLOSE_WAIT;
    }
}

static void tcp_poll(void)
{
    if (tcp.state == TCP_SYN_SENT && tcp.syn_pending &&
        c26_interrupt_ticks() >= tcp.rexmit_deadline) {
        if (tcp.syn_retries >= TCP_SYN_RETRIES) {
            tcp.state = TCP_FAILED;
            tcp.syn_pending = 0;
            return;
        }
        tcp.syn_retries++;
        tcp.rexmit_deadline = c26_interrupt_ticks() + TCP_REXMIT_TICKS;
        send_tcp(TCP_SYN, tcp.snd_una, 0, 0);
    }
}

int c26_tcp_connect(uint32_t ip, uint16_t port)
{
    if (!online) return 0;
    tcp.remote_ip = ip;
    tcp.remote_port = port;
    tcp.local_port = (uint16_t)(40000 + (c26_interrupt_ticks() & 0x3fff));
    uint32_t iss = (uint32_t)(c26_interrupt_ticks() * 2654435761u) ^ 0xC0DE1234u;
    tcp.snd_una = iss;
    tcp.snd_nxt = iss + 1; /* SYN consumes one sequence number */
    tcp.rcv_nxt = 0;
    tcp.rx_len = 0;
    tcp.syn_pending = 1;
    tcp.syn_retries = 0;
    tcp.state = TCP_SYN_SENT;
    tcp.rexmit_deadline = c26_interrupt_ticks() + TCP_REXMIT_TICKS;
    if (!have_gateway_mac) send_arp_request();
    send_tcp(TCP_SYN, iss, 0, 0);
    return 1;
}

int c26_tcp_state(void) { return tcp.state; }
int c26_tcp_connected(void) { return tcp.state == TCP_ESTABLISHED; }

int c26_tcp_send(const void *data, size_t size)
{
    if (tcp.state != TCP_ESTABLISHED) return 0;
    if (size > TCP_DATA_MAX) size = TCP_DATA_MAX;
    send_tcp(TCP_PSH | TCP_ACK, tcp.snd_nxt, data, (uint16_t)size);
    tcp.snd_nxt += size;
    return (int)size;
}

int c26_tcp_recv(void *buf, size_t capacity)
{
    c26_net_poll();
    if (tcp.rx_len == 0) {
        return (tcp.state == TCP_CLOSE_WAIT || tcp.state == TCP_CLOSED ||
                tcp.state == TCP_FAILED)
                   ? -1
                   : 0;
    }
    unsigned int n = tcp.rx_len < capacity ? tcp.rx_len : (unsigned int)capacity;
    memcpy(buf, tcp.rxbuf, n);
    for (unsigned int i = n; i < tcp.rx_len; i++) tcp.rxbuf[i - n] = tcp.rxbuf[i];
    tcp.rx_len -= n;
    return (int)n;
}

void c26_tcp_close(void)
{
    if (tcp.state == TCP_ESTABLISHED || tcp.state == TCP_CLOSE_WAIT) {
        send_tcp(TCP_FIN | TCP_ACK, tcp.snd_nxt, 0, 0);
        tcp.snd_nxt += 1;
        tcp.state = tcp.state == TCP_CLOSE_WAIT ? TCP_CLOSED : TCP_FIN_WAIT;
    } else {
        tcp.state = TCP_CLOSED;
    }
}

/* ------------------------------------------------------------------ */
/* DNS: a tiny blocking A-record resolver over UDP                     */

#define DNS_PORT 53U
#define DNS_CLIENT_PORT 5333U
#define DNS_TIMEOUT_TICKS 300U /* ~3s at the 100 Hz timer */
#define DNS_RETRY_TICKS 50U

int c26_dns_resolve(const char *name, uint32_t *out_ip)
{
    if (!name || !out_ip) return 0;
    if (c26_dns_parse_literal(name, out_ip)) return 1;
    if (!online) return 0;
    uint16_t id = (uint16_t)(c26_interrupt_ticks() ^ 0x2611u);
    uint8_t query[300];
    int qlen = c26_dns_build_query(query, sizeof query, name, id);
    if (qlen <= 0) return 0;
    c26_udp_bind(DNS_CLIENT_PORT); /* idempotent across calls */
    uint8_t resp[512];
    while (c26_udp_recv(DNS_CLIENT_PORT, 0, 0, resp, sizeof resp) >= 0) {
        /* drain any stale datagrams from an earlier resolve */
    }
    uint64_t deadline = c26_interrupt_ticks() + DNS_TIMEOUT_TICKS;
    uint64_t next_send = 0;
    while (c26_interrupt_ticks() < deadline) {
        if (c26_interrupt_ticks() >= next_send) {
            if (!have_gateway_mac) send_arp_request();
            else send_udp(DNS_SERVER, DNS_PORT, DNS_CLIENT_PORT, query,
                          (size_t)qlen);
            next_send = c26_interrupt_ticks() + DNS_RETRY_TICKS;
        }
        int n = c26_udp_recv(DNS_CLIENT_PORT, 0, 0, resp, sizeof resp);
        if (n > 0 && c26_dns_parse_answer(resp, (size_t)n, id, out_ip)) return 1;
        c26_idle();
    }
    return 0;
}

static void handle_frame(const uint8_t *frame, size_t size)
{
    if (size < 14) {
        return;
    }
    uint16_t type = load16(frame + 12);
    if (type == ETH_TYPE_ARP) {
        handle_arp(frame, size);
        return;
    }
    if (type != ETH_TYPE_IP || size < 34) {
        return;
    }
    const uint8_t *ip = frame + 14;
    if ((ip[0] >> 4) != 4 || load32(ip + 16) != OUR_IP) {
        return;
    }
    if (ip[9] == IP_PROTO_ICMP) {
        handle_icmp(frame, ip, size);
    } else if (ip[9] == IP_PROTO_UDP) {
        handle_udp(frame, ip, size);
    } else if (ip[9] == IP_PROTO_TCP) {
        handle_tcp(ip, size);
    }
}

void c26_net_poll(void)
{
    if (!online) {
        return;
    }
    uint32_t id;
    while (c26_virtq_pop(&tx_queue, &id, 0)) {
        tx_pending = 0;
    }
    uint32_t length;
    while (c26_virtq_pop(&rx_queue, &id, &length)) {
        if (id < RX_BUFFERS && length > NET_HDR_BYTES) {
            handle_frame(rx_buffer[id] + NET_HDR_BYTES,
                         length - NET_HDR_BYTES);
        }
        if (id < RX_BUFFERS) {
            c26_virtq_submit(&rx_queue, (uint16_t)id);
        }
    }
    tcp_poll();
    c26_virtio_ack_interrupt(&net_device);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */

int c26_udp_bind(uint16_t port)
{
    if (port == 0 || port == C26_NET_ECHO_PORT || find_binding(port) != 0) {
        return 0;
    }
    for (unsigned int i = 0; i < BINDING_COUNT; i++) {
        if (bindings[i].port == 0) {
            bindings[i].port = port;
            bindings[i].head = 0;
            bindings[i].tail = 0;
            return 1;
        }
    }
    return 0;
}

int c26_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                 const void *data, size_t size)
{
    if (!online || size > C26_UDP_DATA_MAX) {
        return 0;
    }
    if (!have_gateway_mac) {
        send_arp_request();
        return 0;
    }
    send_udp(dst_ip, dst_port, src_port, data, size);
    return 1;
}

int c26_udp_recv(uint16_t port, uint32_t *from_ip, uint16_t *from_port,
                 void *data, size_t capacity)
{
    c26_net_poll();
    udp_binding_t *binding = find_binding(port);
    if (binding == 0 || binding->tail == binding->head) {
        return -1;
    }
    udp_datagram_t *slot = &binding->ring[binding->tail % QUEUE_RING];
    size_t copy = slot->length < capacity ? slot->length : capacity;
    memcpy(data, slot->data, copy);
    if (from_ip != 0) *from_ip = slot->ip;
    if (from_port != 0) *from_port = slot->port;
    binding->tail++;
    return (int)copy;
}

int c26_net_online(void)
{
    return online;
}

int c26_net_init(void)
{
    online = 0;
    if (!c26_virtio_find(VIRTIO_DEVICE_NET, 0, &net_device) ||
        !c26_virtio_begin(&net_device, VIRTIO_NET_F_MAC, 0) ||
        !c26_virtio_queue_setup(&net_device, 0, &rx_queue,
                                rx_memory.descriptors, &rx_memory.available,
                                &rx_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_queue_setup(&net_device, 1, &tx_queue,
                                tx_memory.descriptors, &tx_memory.available,
                                &tx_memory.used, C26_VIRTQ_SIZE) ||
        !c26_virtio_finish(&net_device)) {
        c26_puts("VIRTIO NET: not present\n");
        return 0;
    }
    for (unsigned int i = 0; i < 6; i++) {
        our_mac[i] = (uint8_t)c26_virtio_config_read32(&net_device, i);
    }
    for (uint16_t i = 0; i < RX_BUFFERS; i++) {
        rx_memory.descriptors[i] = (c26_virtq_desc_t){
            (uint64_t)(uintptr_t)rx_buffer[i], sizeof(rx_buffer[i]),
            C26_VIRTQ_DESC_WRITE, 0};
        c26_virtq_submit(&rx_queue, i);
    }
    online = 1;
    send_arp_request();
    c26_puts("VIRTIO NET: online 10.0.2.15 mac=");
    for (unsigned int i = 0; i < 6; i++) {
        static const char hex[] = "0123456789abcdef";
        c26_putc(hex[our_mac[i] >> 4]);
        c26_putc(hex[our_mac[i] & 0xf]);
        if (i != 5) c26_putc(':');
    }
    c26_putc('\n');
    c26_puts("UDP ECHO SERVICE: port 2600\n");
    return 1;
}
