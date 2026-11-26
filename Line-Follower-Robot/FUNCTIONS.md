# Chức năng và kiến trúc

## Luồng

| Luồng | Ưu tiên | Việc | File |
|---|---|---|---|
| control | cooperative 2 | chu kỳ 5 ms: vật cản, cảm biến, lệnh, ngã rẽ, PID, motor | `control_thread.c` |
| HTTP server (Zephyr) | theo Zephyr | file tĩnh, `/api/tables`, nâng cấp WebSocket | `web_server.c` |
| audio | preempt 8 | phát WAV qua I2S | `audio.c` |
| web | preempt 9 | nhận lệnh WebSocket, gửi config/event/telemetry | `web_server.c` |
| network | preempt 10 | WiFi, thử lại, SNTP | `network.c` |
| clock | preempt 12 | TM1637 mỗi giây | `clock_display.c` |

Timer giám sát (`safety.c`) chạy trong ngắt.

## Dữ liệu giữa các luồng

```
trình duyệt → web → web_protocol → g_cmd_queue → control (robot_logic) → motor
control → events (hàng đợi, không chặn) → web → trình duyệt
control/web/audio ↔ g_robot (mutex) → snapshot → telemetry
ISR echo → obstacle_monitor → g_robot.obstacle_*
```

Quy tắc: **chỉ luồng điều khiển đổi chế độ và motor.** Luồng khác chỉ đưa lệnh
vào hàng đợi (ngoại lệ: ghi `notice`). Mọi truy cập `g_robot` phải giữ
`robot_state_lock()`. Không làm việc chậm (flash, mạng) khi đang giữ khoá.

Thứ tự ưu tiên trong một chu kỳ (`robot_logic.c`), giữ nguyên `loop()` bản cũ:
chuyển trạng thái vật cản → lệnh → giữ ở đích → cảm biến → nếu có vật cản thì
dừng → hành động ngã rẽ → bộ dò ngã rẽ (PATH) → STOP → MANUAL → chỉ mắt ngoài
thì giữ lệnh cũ → PID.

## Module

| File | Chức năng | Test trên máy |
|---|---|---|
| `main.c` | thứ tự khởi tạo; motor dừng trước tiên | |
| `robot_config.h` | hằng số thời gian, ngưỡng (giữ tên bản cũ) | |
| `robot_state.c` | trạng thái dùng chung, hàng đợi lệnh, notice | có |
| `motor_control.c` | GPIO/PWM motor, đọc cảm biến, cắt khẩn | |
| `drive.c` | lái và ghi tốc độ, lái tay | có |
| `line_pid.c` | vị trí line, PID, bảng tốc độ lái tay | có |
| `robot_logic.c` | vòng điều khiển, xử lý lệnh, bộ dò ngã rẽ | có |
| `path_control.c` | máy trạng thái tại ngã rẽ | có |
| `obstacle_filter.c` | hysteresis khoảng cách | có |
| `obstacle_monitor.c` | TRIG, ngắt ECHO | |
| `route.c`, `destination_manager.c` | kiểm tra mã, bảng điểm đến | có |
| `tables_storage.c` | LittleFS | |
| `json_flat.c` | parser JSON một cấp, escape | có |
| `web_protocol.c` | lệnh và JSON WebSocket | có |
| `web_server.c` | HTTP, WebSocket | |
| `events.c` | sự kiện tới web | có |
| `network.c` | WiFi, IP/RSSI, SNTP | |
| `wav.c`, `audio.c` | đọc WAV, I2S, amp | `wav.c` |
| `clock_format.c`, `clock_display.c` | đồng hồ | `clock_format.c` |
| `safety.c`, `fatal.c` | watchdog, hạn chót, fault | |
| `bringup_shell.c` | lệnh thử phần cứng | |

## Giao thức WebSocket `/ws`

Lệnh: `{"cmd": "start"|"stop"|"estop"|"drive"|"goto"|"play"|"config"|"tables_reload"}`
với `dir` (drive), `id` và `code` tuỳ chọn (goto), `file` (play), `kp`, `ki`,
`kd`, `base` (config).

Gói gửi về: `config`, `telemetry` (mỗi 100 ms), `event`, `command_result`
(chỉ cho `goto`). Tên trường ở `web_protocol.c`.
