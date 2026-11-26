# PORT_LOG — Line Follower Robot: Arduino → Zephyr

Kế hoạch: `../robot/prompt.txt`. Nguồn tham chiếu (chỉ đọc): `../robot/`.

## T0 — Khảo sát (2026-11-26)

Đã đọc: `AGENTS.md`, toàn bộ `src/` và `data/app.js` của bản Arduino, danh sách mục
trong `mistakes.md` của các project tham khảo. Chưa viết code.

### Môi trường
- west 1.5.0, Zephyr `60dde3fd9b6`, blob WiFi esp32s3 đã có.
- `hello_world` cho `esp32s3_devkitc/esp32s3/procpu` build thành công.
- Không có `ffmpeg`/`ffprobe` trên máy. Không thấy `/dev/ttyACM*`, `/dev/ttyUSB*`.

### Đính chính so với prompt.txt
- Board mặc định khai báo flash **8 MB**, nhưng include bảng phân vùng 4M
  (`storage` 192 KB). Có sẵn `partitions_0x0_amp_8M.dtsi` và `_16M.dtsi`.
- Image `hello_world` có `CONFIG_ESPTOOLPY_FLASHSIZE="2MB"`: phải đặt đúng ở T1.
- TM1637: driver Zephyr hiện được dấu hai chấm qua ký tự `.` sau chữ số thứ hai
  (`"12.34"`), nên không cần viết driver riêng.

### Hành vi trong code cần giữ khi port
- Mất line (5 mắt = 0): `readLine` trả `lastError`, xe **tiếp tục chạy**.
  Mâu thuẫn với câu "motor phải dừng khi mất line" ở mục 0 của prompt.
- `HY_OUT` được cấu hình input nhưng không được đọc.
- Hết thời gian echo / khoảng cách > 400 cm: chỉ dùng để *xoá* trạng thái vật cản
  (sau 2 lần). Cảm biến hỏng hoặc rút dây thì xe không dừng.
- `CMD_STOP`/`CMD_ESTOP` không reset `gDestId`; `CMD_DRIVE` không reset tuyến.
- `config` không kiểm tra miền giá trị Kp/Ki/Kd/base (`driveMotors` kẹp ±255).
- `sendTelemetry` không escape `notice`.
- `updateDestinationHold` chỉ ghi log, không đổi trạng thái motor.
- `connectWiFi` chặn tới 20 s trong `setup()`; bản port sẽ kết nối không chặn.
- PWM: `analogWrite` 0..255 (mặc định core Arduino-ESP32 là 1 kHz, 8 bit; chưa
  kiểm chứng trên máy này vì không có gói PlatformIO).

### JSON phải giữ nguyên
| type | trường |
|---|---|
| config | kp, ki, kd, base, max, on, mode, dest, dir |
| telemetry | s[5], err, pid, l, r, full, on, mode, dir, dest, step, notice, rssi, ip |
| event | message |
| command_result | cmd="goto", ok, message |

### Ánh xạ Arduino → Zephyr
Theo bảng ở mục T0 của `prompt.txt`.

### Âm thanh
`welcome.mp3`: 83 183 byte, 128 kbps, 44,1 kHz stereo, khoảng 5,2 s.
PCM 16 bit: 44,1 kHz stereo ≈ 920 KB; 16 kHz mono ≈ 166 KB.

### Câu hỏi
- Q1: đích là `tests/Line_Follower_Robot/Line_Follower_Robot/` (người dùng chỉ định).
- Q7: mặc định giữ tương thích 100% với `app.js` (R4).
- Q6: mặc định giữ nguyên sơ đồ chân. Lưu ý GPIO48 (ECHO) nối LED RGB trên
  DevKitC-1 v1.0; cần người dùng xác nhận trên board.
- Q5: mặc định WiFi STA như bản cũ.
- Q2, Q3, Q4: đã trả lời (xem dưới).

### Trả lời của người dùng (2026-11-26)
- Q2: ESP32-S3-DevKitC-1. Chưa nêu biến thể; giả định flash 8 MB (N8, mặc định
  của board Zephyr), không dùng PSRAM. **Cần xác nhận trước khi flash.**
- Q3: WAV 16 kHz mono. Máy có GStreamer nên chuyển đổi được tại chỗ (T8).
- Q4: web nhúng gzip trong firmware, `tables.json` trên LittleFS.
- Mất line: giữ hành vi bản cũ (xe tiếp tục theo `last_error`).

## T1 — Khung project (2026-11-26)
- File: `CMakeLists.txt`, `prj.conf`, overlay, `src/main.c`, `README.md`,
  `mistakes.md`, `.gitignore`.
- Overlay thay bảng phân vùng 4M bằng 8M; tắt spi2/spi3/i2c0/i2c1.
- Build sạch. FLASH 136 100 B (1,6 %), DRAM 50 456 B (12,6 %).
- Đã kiểm tra trong `zephyr.dts`: trạng thái các node, phân vùng `slot0`/`storage`.

## T2 — Cấu hình và trạng thái dùng chung (2026-11-26)
- File: `src/robot_config.h`, `src/robot_state.{c,h}`.
- `g_robot` bảo vệ bằng mutex (lồng được), `g_cmd_queue` 8 phần tử.
- `bool` của vật cản và biến PID nằm trong `struct robot_state`. Bảng tuyến và
  trạng thái audio sẽ thuộc module riêng (T4, T8).

## T3 — Motor, cảm biến, PID (2026-11-26)
- File: `src/motor_control.{c,h}`, `src/line_pid.{c,h}` (logic thuần cho T10),
  `src/bringup_shell.c`, `Kconfig`, `bringup.conf`.
- Khác bản cũ: `stop_motors` tắt IN trước rồi mới đưa PWM về 0; lỗi xuất motor
  thì kéo hai chân IN của bên đó về LOW. PWM 1 kHz, mỗi bên một timer LEDC.
- Build sạch, 0 warning. Mặc định: FLASH 137 492 B, DRAM 51 280 B.
  Bring-up: FLASH 144 980 B, DRAM 60 008 B.
- **Chưa chạy trên board. T3 chưa hoàn thành.**

### Cần người dùng kiểm tra trên board
Theo mục "Thử phần cứng (T3)" trong `README.md`:
1. Biến thể flash của module.
2. Giá trị và thứ tự S1..S5 khi trên/ngoài line.
3. Chiều quay từng bánh (tiến, lùi) và đúng bên trái/phải.
4. Motor đứng yên lúc cấp nguồn và reset.

## T4 — Luồng điều khiển và đi theo tuyến (2026-11-26)
- File: `control_thread`, `robot_logic` (thân `loop()`), `path_control`, `drive`,
  `events`, `route`, `destination_manager`, `tables_storage`, `json_flat`,
  `assets/tables.json`, `audio.c` (tạm, thay ở T8).
- Luồng điều khiển `K_PRIO_COOP(2)`, chu kỳ 5 ms theo hạn chót; trễ hạn thì lấy
  mốc mới, luôn ngủ.
- Khác bản cũ: bảng tối đa 32 điểm đến; ghi file qua file tạm rồi đổi tên;
  lỗi đọc cảm biến hoặc PID ra NaN/inf thì dừng (`MODE_STOP`).
- Build sạch, 0 warning. FLASH 141 956 B, DRAM 78 952 B.
- Chưa chạy trên board.

## T5 — HC-SR04 (2026-11-26)
- File: `obstacle_filter` (logic thuần), `obstacle_monitor` (ISR + trigger).
- Giữ nguyên: 10/15 cm, 2 lần không echo mới xoá, `HY_OUT` không đọc; cảm biến
  hỏng/mất dây **không** làm xe dừng (như bản cũ).
- Khác bản cũ: khởi tạo cảm biến lỗi thì không chạy luồng điều khiển.
- Build sạch, 0 warning. FLASH 142 020 B, DRAM 79 032 B.
- **Cần board:** thử vật ở 5 / 12 / 20 cm.

## T6 — WiFi, mDNS, SNTP (2026-11-26)
- File: `network.{c,h}`, `wifi.conf.example`, Kconfig `APP_WIFI_SSID`/`APP_WIFI_PSK`.
- Kết nối không chặn, thử lại mỗi 5 s; có IP thì phát `welcome.mp3`; SNTP
  (pool.ntp.org, time.google.com), đồng bộ lại mỗi giờ, ICT-7.
- Dùng Kconfig riêng của app thay cho `CONFIG_WIFI_CREDENTIALS_STATIC_*` (không
  cần backend lưu credentials).
- Khác bản cũ: hostname DHCP là `robot` (cũ: `robot-line`); không quét và in
  danh sách mạng lúc khởi động.
- Build sạch, 0 warning. FLASH 581 396 B, DRAM 217 520 B (54,5 %).
- **Cần board:** kết nối WiFi, `ping robot.local`, giờ NTP.

## T7 — Web server và WebSocket (2026-11-26)
- File: `web_protocol` (JSON, lệnh), `web_server` (HTTP, REST, WebSocket),
  `sections-rom.ld`, `assets/web/` (chép nguyên từ `robot/data/`).
- Một luồng web gửi/nhận mọi WebSocket; luồng điều khiển chỉ đẩy sự kiện vào
  hàng đợi. Tối đa 4 client (bản cũ: 8).
- Ứng dụng tự trả lời frame CLOSE, gom frame đến nhiều mảnh và cấp bộ đệm nhận
  riêng cho mỗi client; cần header riêng `websocket_internal.h`. Không vá Zephyr.
  (Hai lỗi ban đầu được phát hiện khi mô phỏng, xem `mistakes.md` mục 2 và 3.)
- Khác bản cũ: `notice`/`dir` được escape; STOP/E-STOP vẫn được nhận khi hàng
  đợi đầy (xoá hàng đợi); chỉ phục vụ index/app.js/style.css và /api/tables.
- Build sạch, 0 warning. FLASH 662 020 B, DRAM 274 224 B (68,7 %).
- **Cần board:** mở web, thử từng nút, tải lại trang nhiều lần, lưu bảng tuyến.

## T8 — TM1637 và âm thanh (2026-11-26)
- File: `clock_format`, `clock_display`, `wav`, `audio`, `assets/audio/welcome.wav`;
  overlay bật `i2s0` và `dma`.
- WAV tạo bằng GStreamer từ `robot/data/audio/welcome.mp3`: 16 kHz mono,
  2,26 s, 72 720 byte (ước tính 5,2 s ở T0 là sai: tính cả phần tag ID3).
- TM1637 dùng driver `auxdisplay` của Zephyr, độ sáng 4, dấu hai chấm nháy.
- Chỉ phát được `welcome.mp3`/`welcome.wav` (nhúng sẵn).
- Driver I2S: DROP làm mất block đang truyền, nên slab được khởi tạo lại.
- Build sạch, 0 warning. FLASH ~736 KB, DRAM 301 528 B (75,6 %).
- **Cần board:** nghe âm thanh, xem đồng hồ.

## T9 — An toàn và độ bền (2026-11-26)
- File: `safety` (watchdog + giám sát hạn chót), `fatal` (handler lỗi),
  `debug.conf` (thread analyzer), `CONFIG_STACK_SENTINEL`.
- Luồng điều khiển trễ > 100 ms: timer kéo IN1..IN4 xuống LOW. Không được nuôi:
  watchdog reset (ngắt ở 500 ms, reset ở khoảng 1 s). Fault: cắt motor, reset.
- Không khởi tạo được watchdog thì luồng điều khiển không chạy.
- Đã kiểm tra bằng `nm`: handler của app thay handler mặc định.
- Build sạch, 0 warning (mặc định, debug, bring-up).
  Mặc định: FLASH 736 228 B (8,8 %), DRAM 303 416 B (76,0 %).
- **Cần board:** chưa đo stack thật; chưa thử watchdog, fault.

## T10 — Test logic (2026-11-26)
- `tests/logic/`: ztest trên `native_sim/native/64`, dùng mã thật của firmware,
  giả lập motor, âm thanh, bộ nhớ.
- 41/41 đạt, cả khi bật AddressSanitizer + UBSan (có ca nhồi 40 000 đầu vào hỏng).
- Mô phỏng tạm (không lưu trong project): `web_server.c` chạy trên `native_sim`
  với socket của máy host (cổng 18080), client Python: 35/35 đạt (file tĩnh,
  REST, WebSocket, 4 client, frame cắt nhỏ, 80 lần đóng/mở, client không đọc);
  thêm 60 lần CLOSE hợp lệ và 60 lần CLOSE thiếu FIN, server đóng TCP đủ.
  Socket host khác ngăn xếp TCP của Zephyr trên board nên vẫn phải thử lại.

## T11 — Tài liệu (2026-11-26)
- `README.md`, `FUNCTIONS.md`, `mistakes.md`.

## Kết quả build cuối (0 warning)
| Bản | FLASH | DRAM |
|---|---|---|
| mặc định | 736 228 B (8,8 %) | 303 416 B (76,0 %) |
| wifi + debug | 736 788 B | 306 848 B (76,9 %) |
| bring-up | 657 940 B | 239 608 B (60,0 %) |

DRAM dưới ngưỡng 80 % nhưng không còn nhiều.

## Rà R5 (an toàn)
| Yêu cầu | Cách đáp ứng | Test máy | Board |
|---|---|---|---|
| Motor dừng khi khởi động | `stop_motors()` đầu `main()`; trước đó chân thả nổi | - | chưa |
| ESTOP | lệnh vào hàng đợi, không bị bỏ khi đầy | có | chưa |
| MANUAL timeout 600 ms | `robot_logic.c` | có | chưa |
| Turn timeout 1,5 s | `path_control.c` | có | chưa |
| Vật cản | dừng ở mọi chế độ | có (logic) | chưa |
| Luồng điều khiển treo | cắt IN sau 100 ms; watchdog reset | - | chưa |
| Điều khiển ưu tiên hơn mạng/audio | cooperative 2 | - | chưa đo |
| Mất line | giữ như bản cũ (người dùng chọn) | có | chưa |

## Bảng đối chiếu hành vi
"Test máy": logic (ztest) hoặc mô phỏng web trên `native_sim`.

| Tính năng | Port | Build | Test máy | Board |
|---|---|---|---|---|
| Đọc 5 cảm biến | có | có | - | chưa |
| Motor L298N (PWM, chiều) | có | có | - | chưa |
| PID, chỉ mắt ngoài, mất line | có | có | có | chưa |
| Chế độ STOP/AUTO/MANUAL/PATH | có | có | có | chưa |
| Mã tuyến, ngã rẽ S/L/R/0 | có | có | có | chưa |
| Bảng điểm đến, `/api/tables` | có | có | có (bộ nhớ giả) | chưa |
| LittleFS | có | có | - | chưa |
| HC-SR04 | có | có | chỉ hysteresis | chưa |
| WiFi, thử lại, IP/RSSI | có | có | - | chưa |
| mDNS `robot.local` | có | có | - | chưa |
| NTP, TM1637 | có | có | chỉ định dạng giờ | chưa |
| Âm thanh I2S | có | có | chỉ đọc WAV | chưa |
| Web, WebSocket | có | có | mô phỏng | chưa |
| Watchdog, fault handler | có | có | - | chưa |

## Việc còn treo (cần board)
Theo mục "Kiểm tra trên board" của `README.md`. T3, T5, T6, T7, T8, T9 chưa được
coi là hoàn thành. Cần xác nhận module có flash 8 MB.
