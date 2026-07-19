#ifndef C26_NET_H
#define C26_NET_H

#include <stddef.h>
#include <stdint.h>

/* Real networking: virtio-net plus a deliberately small IPv4 stack — ARP,
 * ICMP echo reply, and UDP. QEMU's user network gives the guest 10.0.2.15
 * with gateway 10.0.2.2; a kernel UDP echo service on port 2600 makes the
 * whole path verifiable from the host through hostfwd. */

#define C26_NET_ECHO_PORT 2600U
#define C26_UDP_DATA_MAX 476U

int c26_net_init(void);
int c26_net_online(void);
void c26_net_poll(void);

int c26_udp_bind(uint16_t port);
int c26_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                 const void *data, size_t size);
int c26_udp_recv(uint16_t port, uint32_t *from_ip, uint16_t *from_port,
                 void *data, size_t capacity);

/* Minimal single-connection TCP client. connect() initiates the handshake
 * (non-blocking); poll the stack (c26_net_poll) and check c26_tcp_connected
 * until established or c26_tcp_state() reports failure. recv returns bytes,
 * 0 when the connection is open but idle, or -1 at EOF/closed. */
int c26_tcp_connect(uint32_t ip, uint16_t port);
int c26_tcp_connected(void);
int c26_tcp_state(void); /* 0 closed, 2 established, 5 failed (see net.c) */
int c26_tcp_send(const void *data, size_t size);
int c26_tcp_recv(void *buf, size_t capacity);
void c26_tcp_close(void);

/* Blocking (with timeout) DNS A-record resolver over UDP to QEMU user-net's
 * 10.0.2.3:53. Accepts a dotted-quad literal directly. Returns 1 and fills
 * *out_ip (host byte order) on success, 0 on failure/timeout. */
int c26_dns_resolve(const char *name, uint32_t *out_ip);

#endif
