#pragma once

/*
 * Sự kiện từ các luồng nghiệp vụ tới luồng web. Không bao giờ chặn: không có
 * ai đọc hoặc hàng đợi đầy thì sự kiện bị bỏ.
 */

#include <stdbool.h>

#include <zephyr/toolchain.h>

#define EVENT_MESSAGE_LEN 112

/* Ghi log và gửi tới ô "event log" trên web */
__printf_like(1, 2) void event_log(const char *fmt, ...);

/* Lấy một sự kiện đang chờ. Trả về false nếu hết. */
bool event_take(char message[EVENT_MESSAGE_LEN]);

/* Yêu cầu phát lại gói "config" cho mọi client (không bị mất khi hàng đợi đầy) */
void event_request_config(void);
bool event_take_config_request(void);
