#include "c26.h"
#include "c26_devices.h"

void c26_robot_set_motors(c26_robot_state_t *robot, int left, int right)
{
    if (left < -100) left = -100;
    if (left > 100) left = 100;
    if (right < -100) right = -100;
    if (right > 100) right = 100;
    robot->left_motor = left;
    robot->right_motor = right;
    c26_device_write8(C26_REG_LEFT_MOTOR, (uint8_t)(left + 100));
    c26_device_write8(C26_REG_RIGHT_MOTOR, (uint8_t)(right + 100));
    c26_can_frame_t command = {
        0x200, 2, {(uint8_t)(left + 100), (uint8_t)(right + 100),
                   0, 0, 0, 0, 0, 0}};
    c26_can_send(&command);
    c26_puts("[ROBOT] motors left=");
    c26_put_uint((uint64_t)(left < 0 ? -left : left));
    c26_puts(left < 0 ? " reverse" : " forward");
    c26_puts(" right=");
    c26_put_uint((uint64_t)(right < 0 ? -right : right));
    c26_puts(right < 0 ? " reverse\n" : " forward\n");
}

void c26_robot_sample_sensors(c26_robot_state_t *robot)
{
    uint8_t front = 0;
    uint8_t floor = 0;
    c26_i2c_read_register(0x42, C26_REG_FRONT_SENSOR, &front);
    c26_i2c_read_register(0x42, C26_REG_FLOOR_SENSOR, &floor);
    robot->sensor_front = front;
    robot->sensor_floor = floor;
    c26_puts("[ROBOT] sensors front=");
    c26_put_uint(front);
    c26_puts(" floor=");
    c26_put_uint(floor);
    c26_uart_putc('\n');
}

void c26_robot_demo(void)
{
    c26_robot_state_t robot = {0, 0, 0, 0};
    c26_puts("ROBOT SDK DEMO\n");
    c26_robot_sample_sensors(&robot);
    c26_robot_set_motors(&robot, 42, 42);
    c26_puts("[ROBOT] stateful I2C + CAN control path ready\n");
}
