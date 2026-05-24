#include "c26.h"
#include "c26_devices.h"

static const char *bus_name(c26_bus_kind_t kind)
{
    switch (kind) {
    case C26_BUS_USB:
        return "USB";
    case C26_BUS_I2C:
        return "I2C";
    case C26_BUS_CAN:
        return "CAN";
    case C26_BUS_TCPIP:
        return "TCP/IP";
    default:
        return "UNKNOWN";
    }
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

void c26_tcpip_open_demo_socket(const char *host, uint16_t port)
{
    c26_puts("[TCP] open demo socket ");
    c26_puts(host);
    c26_puts(":");
    c26_put_uint(port);
    c26_puts(" (emulated)\n");
}

void c26_devices_demo(void)
{
    c26_puts("2026 DEVICE FABRIC\n");
    c26_device_init((c26_device_t){C26_BUS_USB, "keyboard+storage", 0x10, 480000000});
    c26_device_init((c26_device_t){C26_BUS_I2C, "sensor-bus", 0x42, 400000});
    c26_device_init((c26_device_t){C26_BUS_CAN, "robot-can", 0x26, 1000000});
    c26_tcpip_open_demo_socket("demo.c26.local", 6426);
}
