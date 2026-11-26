#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "robot_state.h"

struct fake_hw {
	int left; /* đầu ra motor hiện tại */
	int right;
	int audio_requests;
	char audio_file[80];
	char file[4096]; /* tables.json trong RAM */
	int file_len;    /* < 0: chưa có file */
	bool fail_write;
};

extern struct fake_hw fake;

/* Đưa trạng thái robot, hàng đợi, logic và phần cứng giả về ban đầu */
void fake_reset(void);

/* true nếu từ lần fake_reset có sự kiện chứa chuỗi này (đọc hết hàng đợi) */
bool event_seen(const char *needle);
