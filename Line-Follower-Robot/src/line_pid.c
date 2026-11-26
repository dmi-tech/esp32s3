#include <string.h>

#include <zephyr/sys/util.h>

#include "line_pid.h"

float line_position(const uint8_t sensor[5], float last_error)
{
	static const int weight[5] = {-2, -1, 0, 1, 2};
	int total = 0;
	int count = 0;

	for (int i = 0; i < 5; i++) {
		if (sensor[i] != 0U) {
			total += weight[i];
			count++;
		}
	}

	if (count == 0) {
		return last_error;
	}

	return (float)total / (float)count;
}

void pid_reset(struct robot_state *s, uint32_t now_ms)
{
	s->integral = 0.0f;
	s->last_error = 0.0f;
	s->error = 0.0f;
	s->last_pid_ms = now_ms;
}

float pid_calculate(struct robot_state *s, float current_error, uint32_t now_ms)
{
	float dt = (float)(now_ms - s->last_pid_ms) / 1000.0f;

	if (dt <= 0.0f) {
		dt = 0.001f;
	}

	s->last_pid_ms = now_ms;

	s->integral = CLAMP(s->integral + current_error * dt, -PID_INTEGRAL_LIMIT,
			    PID_INTEGRAL_LIMIT);

	const float derivative = (current_error - s->last_error) / dt;

	s->last_error = current_error;

	return s->kp * current_error + s->ki * s->integral + s->kd * derivative;
}

void manual_drive_speeds(const char *dir, int base_speed, int max_speed, int *left,
			 int *right)
{
	/* Cùng thứ tự kẹp như constrain(baseSpeed, 0, maxSpeed) */
	const int fast = base_speed < 0 ? 0 : MIN(base_speed, max_speed);
	const int slow = fast / 2;
	const struct {
		const char *dir;
		int left;
		int right;
	} table[] = {
		{"forward", fast, fast},        {"back", -fast, -fast},
		{"left", 0, fast},              {"right", fast, 0},
		{"forward_left", slow, fast},   {"forward_right", fast, slow},
		{"back_left", -slow, -fast},    {"back_right", -fast, -slow},
	};

	*left = 0;
	*right = 0;

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (strcmp(dir, table[i].dir) == 0) {
			*left = table[i].left;
			*right = table[i].right;
			return;
		}
	}
}
