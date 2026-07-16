#include "c26.h"
#include "c26_devices.h"

#define DEVICE_REGISTER_COUNT 256U
#define CAN_QUEUE_DEPTH 8U
#define PACKET_QUEUE_DEPTH 4U
#define PACKET_PAYLOAD_SIZE 64U

typedef struct {
    uint16_t port;
    uint16_t length;
    uint8_t bytes[PACKET_PAYLOAD_SIZE];
} packet_t;

static uint8_t device_registers[DEVICE_REGISTER_COUNT];
static c26_can_frame_t can_queue[CAN_QUEUE_DEPTH];
static unsigned int can_read_index;
static unsigned int can_write_index;
static unsigned int can_count;
static packet_t packet_queue[PACKET_QUEUE_DEPTH];
static unsigned int packet_read_index;
static unsigned int packet_write_index;
static unsigned int packet_count;

static const char *bus_name(c26_bus_kind_t kind)
{
    switch (kind) {
    case C26_BUS_USB: return "USB";
    case C26_BUS_I2C: return "I2C";
    case C26_BUS_CAN: return "CAN";
    case C26_BUS_TCPIP: return "TCP/IP";
    default: return "UNKNOWN";
    }
}

void c26_device_fabric_init(void)
{
    for (unsigned int i = 0; i < DEVICE_REGISTER_COUNT; i++) {
        device_registers[i] = 0;
    }
    can_read_index = can_write_index = can_count = 0;
    packet_read_index = packet_write_index = packet_count = 0;
    device_registers[C26_REG_FRONT_SENSOR] = 26;
    device_registers[C26_REG_FLOOR_SENSOR] = 64;
}

void c26_device_init(c26_device_t device)
{
    c26_puts("[DEV] ");
    c26_puts(bus_name(device.kind));
    c26_puts(" ");
    c26_puts(device.name);
    c26_puts(" addr=");
    c26_put_hex(device.address);
    c26_puts(" speed=");
    c26_put_uint(device.speed_hz);
    c26_puts("Hz\n");
}

int c26_device_write8(uint16_t reg, uint8_t value)
{
    if (reg >= DEVICE_REGISTER_COUNT) {
        return 0;
    }
    device_registers[reg] = value;
    return 1;
}

int c26_device_read8(uint16_t reg, uint8_t *value)
{
    if (reg >= DEVICE_REGISTER_COUNT || value == 0) {
        return 0;
    }
    *value = device_registers[reg];
    return 1;
}

int c26_i2c_write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    if (address != 0x42) {
        return 0;
    }
    return c26_device_write8(reg, value);
}

int c26_i2c_read_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    if (address != 0x42) {
        return 0;
    }
    return c26_device_read8(reg, value);
}

int c26_can_send(const c26_can_frame_t *frame)
{
    if (frame == 0 || frame->length > 8 || can_count == CAN_QUEUE_DEPTH) {
        return 0;
    }
    can_queue[can_write_index] = *frame;
    can_write_index = (can_write_index + 1) % CAN_QUEUE_DEPTH;
    can_count++;
    return 1;
}

int c26_can_receive(c26_can_frame_t *frame)
{
    if (frame == 0 || can_count == 0) {
        return 0;
    }
    *frame = can_queue[can_read_index];
    can_read_index = (can_read_index + 1) % CAN_QUEUE_DEPTH;
    can_count--;
    return 1;
}

int c26_network_send(uint16_t port, const uint8_t *data, uint16_t length)
{
    if (data == 0 || length > PACKET_PAYLOAD_SIZE ||
        packet_count == PACKET_QUEUE_DEPTH) {
        return 0;
    }
    packet_t *packet = &packet_queue[packet_write_index];
    packet->port = port;
    packet->length = length;
    for (uint16_t i = 0; i < length; i++) {
        packet->bytes[i] = data[i];
    }
    packet_write_index = (packet_write_index + 1) % PACKET_QUEUE_DEPTH;
    packet_count++;
    return 1;
}

int c26_network_receive(uint16_t *port, uint8_t *data, uint16_t *length)
{
    if (port == 0 || data == 0 || length == 0 || packet_count == 0) {
        return 0;
    }
    packet_t *packet = &packet_queue[packet_read_index];
    if (*length < packet->length) {
        return 0;
    }
    *port = packet->port;
    *length = packet->length;
    for (uint16_t i = 0; i < packet->length; i++) {
        data[i] = packet->bytes[i];
    }
    packet_read_index = (packet_read_index + 1) % PACKET_QUEUE_DEPTH;
    packet_count--;
    return 1;
}

void c26_tcpip_open_demo_socket(const char *host, uint16_t port)
{
    c26_puts("[TCP] loopback endpoint ");
    c26_puts(host);
    c26_puts(":");
    c26_put_uint(port);
    c26_uart_putc('\n');
}

void c26_devices_demo(void)
{
    c26_device_fabric_init();
    c26_puts("2026 DEVICE FABRIC\n");
    c26_device_init((c26_device_t){C26_BUS_USB, "virtio-input", 0x10, 12000000});
    c26_device_init((c26_device_t){C26_BUS_I2C, "sensor-bus", 0x42, 400000});
    c26_device_init((c26_device_t){C26_BUS_CAN, "robot-loopback", 0x26, 1000000});
    c26_tcpip_open_demo_socket("loopback.c26", 6426);

    c26_i2c_write_register(0x42, C26_REG_USER0, 42);
    uint8_t value = 0;
    c26_i2c_read_register(0x42, C26_REG_USER0, &value);
    c26_puts("DEVICE SDK: register readback=");
    c26_put_uint(value);
    c26_uart_putc('\n');

    c26_can_frame_t sent = {0x126, 2, {26, 42, 0, 0, 0, 0, 0, 0}};
    c26_can_frame_t received;
    if (c26_can_send(&sent) && c26_can_receive(&received)) {
        c26_puts("CAN SDK: loopback frame received\n");
    }

    static const uint8_t message[] = {'c', '2', '6'};
    uint8_t reply[PACKET_PAYLOAD_SIZE];
    uint16_t port = 0;
    uint16_t length = sizeof(reply);
    if (c26_network_send(6426, message, sizeof(message)) &&
        c26_network_receive(&port, reply, &length) && port == 6426 &&
        length == sizeof(message)) {
        c26_puts("TCP/IP SDK: packet loopback received\n");
    }
}
