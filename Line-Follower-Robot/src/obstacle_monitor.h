#pragma once

#include <stdint.h>

int obstacle_monitor_init(void);

/* Gọi mỗi chu kỳ từ luồng điều khiển; cập nhật g_robot.obstacle_*. */
void obstacle_monitor_update(uint32_t now_ms);
