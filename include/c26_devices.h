#ifndef C26_DEVICES_H
#define C26_DEVICES_H

#include <stdint.h>

typedef enum {
    C26_BUS_USB = 1,
    C26_BUS_I2C = 2,
    C26_BUS_CAN = 3,
    C26_BUS_TCPIP = 4
} c26_bus_kind_t;

typedef struct {
    c26_bus_kind_t kind;
    const char *name;
    uint32_t address;
    uint32_t speed_hz;
} c26_device_t;

typedef struct {
    int left_motor;
    int right_motor;
    int sensor_front;
    int sensor_floor;
} c26_robot_state_t;

void c26_device_init(c26_device_t device);
void c26_tcpip_open_demo_socket(const char *host, uint16_t port);
void c26_robot_set_motors(c26_robot_state_t *robot, int left, int right);
void c26_robot_sample_sensors(c26_robot_state_t *robot);

#endif
