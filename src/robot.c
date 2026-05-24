#include "c26.h"
#include "c26_devices.h"

void c26_robot_set_motors(c26_robot_state_t *robot, int left, int right)
{
    robot->left_motor = left;
    robot->right_motor = right;
    c26_puts("[ROBOT] motors left=");
    c26_put_uint((uint64_t)left);
    c26_puts(" right=");
    c26_put_uint((uint64_t)right);
    c26_uart_putc('\n');
}

void c26_robot_sample_sensors(c26_robot_state_t *robot)
{
    robot->sensor_front = 26;
    robot->sensor_floor = 64;
    c26_puts("[ROBOT] sensors front=");
    c26_put_uint((uint64_t)robot->sensor_front);
    c26_puts(" floor=");
    c26_put_uint((uint64_t)robot->sensor_floor);
    c26_uart_putc('\n');
}

void c26_robot_demo(void)
{
    c26_robot_state_t robot = {0, 0, 0, 0};
    c26_puts("ROBOT SDK DEMO\n");
    c26_robot_sample_sensors(&robot);
    c26_robot_set_motors(&robot, 42, 42);
    c26_puts("[ROBOT] square path primitive ready\n");
}
