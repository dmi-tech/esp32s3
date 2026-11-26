#pragma once

/*
 * Máy trạng thái tại ngã rẽ. Mọi hàm yêu cầu người gọi đang giữ khoá trạng
 * thái; thời gian được truyền vào để test được.
 */

#include <stdbool.h>
#include <stdint.h>

void path_control_reset(void);
void path_arrived(const char *why);
void path_handle_junction(bool left_on, bool right_on, uint32_t now_ms);

/* true khi hành động tại ngã rẽ đang điều khiển motor */
bool path_update_action(uint32_t now_ms);

/* Cộng bù thời gian tạm dừng vì vật cản */
void path_adjust_action_clock(uint32_t pause_ms);
void path_cancel_action(void);
void path_update_destination_hold(uint32_t now_ms);
