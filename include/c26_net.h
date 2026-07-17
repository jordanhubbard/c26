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

#endif
