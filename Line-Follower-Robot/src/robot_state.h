#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "robot_config.h"

enum robot_mode {
	MODE_STOP,
	MODE_AUTO,
	MODE_MANUAL,
	MODE_PATH,
};

enum command_type {
	CMD_NONE = 0,
	CMD_START,
	CMD_STOP,
	CMD_ESTOP,
	CMD_DRIVE,
	CMD_GOTO,
	CMD_PLAY,
	CMD_CONFIG,
};

struct command {
	uint8_t type;
	char text[96];
	int value;
	float kp;
	float ki;
	float kd;
	int base;
};

#define NOTICE_LEN 96
#define DRIVE_DIR_LEN 16
#define COMMAND_QUEUE_LEN 8

struct robot_state {
	/* Thông số chỉnh qua web */
	float kp;
	float ki;
	float kd;
	int base_speed;
	int max_speed;

	/* Chế độ */
	bool enabled;
	enum robot_mode mode;
	char drive_dir[DRIVE_DIR_LEN];
	uint32_t last_drive_cmd_ms;

	/* Cảm biến và đầu ra */
	uint8_t sensor[5]; /* 1 = thấy line */
	float correction;
	int left_speed;
	int right_speed;
	bool full_line;
	char notice[NOTICE_LEN];

	/* PID */
	float error;
	float last_error;
	float integral;
	uint32_t last_pid_ms;

	/* Tuyến đường */
	char path[ROUTE_CODE_MAX_LEN + 1];
	size_t path_idx;
	int dest_id;
	int turn; /* -1 trái, 1 phải, 0 không rẽ */

	/* Bộ dò ngã rẽ */
	bool junction_armed;
	uint32_t junction_since_ms;
	uint32_t centered_since_ms;

	/* Vật cản */
	bool obstacle_detected;
	float obstacle_distance_cm;
};

/*
 * Trạng thái dùng chung giữa các luồng. Chỉ đọc/ghi khi đang giữ khoá
 * (khoá lồng nhau được). Chỉ luồng điều khiển được đổi chế độ và motor.
 */
extern struct robot_state g_robot;

/* Lệnh từ web tới luồng điều khiển */
extern struct k_msgq g_cmd_queue;

void robot_state_lock(void);
void robot_state_unlock(void);

/* Bản sao nhất quán cho luồng đọc (telemetry) */
void robot_state_snapshot(struct robot_state *out);

void robot_set_notice(const char *text);
__printf_like(1, 2) void robot_set_noticef(const char *fmt, ...);

/* Chép chuỗi, cắt bớt cho vừa bộ đệm (như strlcpy của bản cũ) */
void robot_copy_text(char *dst, size_t size, const char *src);

const char *robot_mode_name(enum robot_mode mode);
