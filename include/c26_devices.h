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

typedef struct {
    uint32_t identifier;
    uint8_t length;
    uint8_t data[8];
} c26_can_frame_t;

enum {
    C26_REG_FRONT_SENSOR = 0x20,
    C26_REG_FLOOR_SENSOR = 0x21,
    C26_REG_LEFT_MOTOR = 0x30,
    C26_REG_RIGHT_MOTOR = 0x31,
    C26_REG_USER0 = 0x80,
};

void c26_device_fabric_init(void);
void c26_device_init(c26_device_t device);
int c26_device_write8(uint16_t reg, uint8_t value);
int c26_device_read8(uint16_t reg, uint8_t *value);
int c26_i2c_write_register(uint8_t address, uint8_t reg, uint8_t value);
int c26_i2c_read_register(uint8_t address, uint8_t reg, uint8_t *value);
int c26_can_send(const c26_can_frame_t *frame);
int c26_can_receive(c26_can_frame_t *frame);
int c26_network_send(uint16_t port, const uint8_t *data, uint16_t length);
int c26_network_receive(uint16_t *port, uint8_t *data, uint16_t *length);
void c26_tcpip_open_demo_socket(const char *host, uint16_t port);
void c26_robot_set_motors(c26_robot_state_t *robot, int left, int right);
void c26_robot_sample_sensors(c26_robot_state_t *robot);

#endif
