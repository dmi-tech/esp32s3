#pragma once

/*
 * Logic thuần (không đụng phần cứng) để test được trên máy.
 * Người gọi phải giữ khoá trạng thái nếu truyền vào g_robot.
 */

#include "robot_state.h"

/* Vị trí line -2..2; không mắt nào thấy line thì trả lại last_error. */
float line_position(const uint8_t sensor[5], float last_error);

/* Xoá trạng thái PID; now_ms là mốc tính dt kế tiếp. */
void pid_reset(struct robot_state *s, uint32_t now_ms);

/* Trả về lượng hiệu chỉnh, cập nhật integral/last_error/last_pid_ms. */
float pid_calculate(struct robot_state *s, float current_error, uint32_t now_ms);

/* Tốc độ hai bánh cho hướng lái tay; hướng lạ = đứng yên. */
void manual_drive_speeds(const char *dir, int base_speed, int max_speed, int *left,
			 int *right);
