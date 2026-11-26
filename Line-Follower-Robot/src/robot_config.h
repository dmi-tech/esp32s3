#pragma once

/*
 * Giữ nguyên tên và giá trị hằng số của bản Arduino.
 * Chân GPIO nằm ở boards/esp32s3_devkitc_esp32s3_procpu.overlay.
 * SSID/mật khẩu WiFi không đặt trong mã nguồn.
 */

/* Tên mDNS/hostname: CONFIG_NET_HOSTNAME trong prj.conf */

/* Mức chân cảm biến khi thấy line */
#define LINE_ON 1

#define JUNCTION_STOP_MS 2000U
#define ROUTE_ACTION_MS 500U
#define ROUTE_TURN_TIMEOUT_MS 1500U
#define ROUTE_TURN_SENSOR_STABLE_MS 30U
#define JUNCTION_APPROACH_PERCENT 50
#define LINE_LOOP_DELAY_MS 5
#define JUNCTION_STABLE_MS 30U
#define CENTER_ARM_MS 200U
#define DESTINATION_HOLD_MS 5000U
#define TELEMETRY_INTERVAL 100U
#define DRIVE_TIMEOUT 600U
#define OBSTACLE_STOP_CM 10.0f
#define OBSTACLE_CLEAR_CM 15.0f
#define OBSTACLE_NO_ECHO_CLEAR_COUNT 2
#define OBSTACLE_PING_INTERVAL_MS 60U
#define OBSTACLE_ECHO_TIMEOUT_MS 35U
#define NTP_TIMEZONE_OFFSET_S (7 * 3600) /* ICT-7 */

#define ROUTE_CODE_MAX_LEN 90
#define MOTOR_SPEED_LIMIT 255
#define PID_INTEGRAL_LIMIT 10.0f
