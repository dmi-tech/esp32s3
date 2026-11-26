#pragma once

/*
 * Thân vòng lặp điều khiển (loop() của bản Arduino), không đụng phần cứng
 * trực tiếp ngoài motor_output()/stop_motors().
 */

#include <stdint.h>

#include "robot_state.h"

void robot_logic_reset(void);

/*
 * Một chu kỳ điều khiển. Trước khi gọi, g_robot.obstacle_detected và
 * obstacle_distance_cm phải được cập nhật.
 */
void robot_logic_step(uint32_t now_ms, const uint8_t sensor[5]);

void robot_apply_command(const struct command *c, uint32_t now_ms);

/* Dừng an toàn khi phần cứng lỗi */
void robot_fail_stop(const char *why);
