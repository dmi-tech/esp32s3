#pragma once

/*
 * Giao thức JSON qua WebSocket, giữ nguyên bản cũ để app.js không đổi.
 * Không đụng socket nên test được trên máy.
 */

#include <stdbool.h>
#include <stddef.h>

#include "robot_state.h"

struct web_reply {
	bool send_goto_result;
	bool ok;
	char message[NOTICE_LEN];
};

/* Phân tích, kiểm tra và đưa lệnh vào g_cmd_queue. Không đổi chế độ/motor. */
void web_handle_command(const char *text, size_t len, struct web_reply *reply);

/* Các hàm dưới trả về độ dài, hoặc -ENOSPC */
int web_config_json(char *buf, size_t size, const struct robot_state *s);
int web_telemetry_json(char *buf, size_t size, const struct robot_state *s, const char *ip,
		       int rssi);
int web_event_json(char *buf, size_t size, const char *message);
int web_goto_result_json(char *buf, size_t size, bool ok, const char *message);
