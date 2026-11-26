#include <stdarg.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "robot_state.h"

struct robot_state g_robot = {
	.kp = 35.0f,
	.ki = 0.0f,
	.kd = 20.0f,
	.base_speed = 150,
	.max_speed = 255,
	.mode = MODE_STOP,
	.drive_dir = "stop",
};

K_MSGQ_DEFINE(g_cmd_queue, sizeof(struct command), COMMAND_QUEUE_LEN, 4);

static K_MUTEX_DEFINE(state_mutex);

void robot_state_lock(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
}

void robot_state_unlock(void)
{
	k_mutex_unlock(&state_mutex);
}

void robot_state_snapshot(struct robot_state *out)
{
	robot_state_lock();
	*out = g_robot;
	robot_state_unlock();
}

void robot_copy_text(char *dst, size_t size, const char *src)
{
	if (size == 0U) {
		return;
	}

	size_t len = 0;

	while (len < size - 1U && src[len] != '\0') {
		len++;
	}

	memmove(dst, src, len);
	dst[len] = '\0';
}

void robot_set_notice(const char *text)
{
	robot_state_lock();
	robot_copy_text(g_robot.notice, sizeof(g_robot.notice), text);
	robot_state_unlock();
}

void robot_set_noticef(const char *fmt, ...)
{
	va_list args;

	robot_state_lock();
	va_start(args, fmt);
	(void)vsnprintk(g_robot.notice, sizeof(g_robot.notice), fmt, args);
	va_end(args);
	robot_state_unlock();
}

const char *robot_mode_name(enum robot_mode mode)
{
	switch (mode) {
	case MODE_AUTO:
		return "auto";
	case MODE_MANUAL:
		return "manual";
	case MODE_PATH:
		return "path";
	default:
		return "stop";
	}
}
