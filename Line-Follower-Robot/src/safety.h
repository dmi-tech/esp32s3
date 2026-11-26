#pragma once

/*
 * Hai lớp bảo vệ khi luồng điều khiển bị treo:
 *  - hạn chót SAFETY_DEADLINE_MS: ngắt timer kéo IN1..IN4 xuống LOW;
 *  - watchdog phần cứng: reset SoC nếu vẫn không được nuôi.
 * Lỗi nghiêm trọng (fault) do fatal.c xử lý: dừng motor rồi reset.
 */

#define SAFETY_DEADLINE_MS 100U
/* Driver ESP32: ngắt ở mốc này, reset ở gấp đôi */
#define SAFETY_WATCHDOG_MS 500U

/* Bật watchdog và bộ giám sát; gọi ngay trước khi tạo luồng điều khiển. */
int safety_start(void);

/* Luồng điều khiển gọi mỗi chu kỳ */
void safety_heartbeat(void);
