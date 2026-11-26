# Robot dò line ESP32-S3 (Zephyr)

Bản port sang Zephyr RTOS của firmware Arduino trong `../robot/`: dò line 5 mắt
bằng PID, đi theo mã tuyến, lái tay qua web, tránh vật cản siêu âm, đồng hồ
TM1637 và phát âm thanh.

> **Trạng thái:** đã port đủ chức năng, build sạch, logic đã test trên máy.
> **Chưa chạy trên board thật.** Làm theo mục "Kiểm tra trên board" trước khi
> cho xe chạy.

Tài liệu liên quan: `FUNCTIONS.md` (kiến trúc), `PORT_LOG.md` (tiến độ, đối
chiếu hành vi), `mistakes.md` (lỗi đã gặp), `../robot/prompt.txt` (kế hoạch).

## Phần cứng

ESP32-S3-DevKitC-1, không dùng PSRAM. Board
`esp32s3_devkitc/esp32s3/procpu`.

| Khối | Chân |
|---|---|
| Cảm biến line S1..S5 (HIGH = thấy line) | GPIO 4, 5, 6, 7, 15 |
| L298N | ENA=16, IN1=17, IN2=18, IN3=8, IN4=3, ENB=46 |
| MAX98357 | LRC=9, BCLK=10, DIN=11, GAIN=12, SD=13 |
| TM1637 | CLK=1, DIO=2 |
| HC-SR04 | TRIG=45, ECHO=48, OUT=47 |

Lưu ý:
- GPIO 3, 45, 46 là chân strapping; mạch ngoài không được kéo đổi mức lúc reset.
- Trước khi phần mềm khởi tạo (lúc boot, sau reset), chân motor không được lái.
  Nên có điện trở kéo xuống ở ENA/ENB và IN1..IN4.
- ECHO của HC-SR04 là 5 V, cần chia áp xuống 3,3 V.
- DevKitC-1 v1.0 dùng GPIO48 cho LED RGB, trùng chân ECHO.

## Build

```bash
cp wifi.conf.example wifi.conf      # điền SSID/mật khẩu mạng 2,4 GHz
west build -p always -b esp32s3_devkitc/esp32s3/procpu . -- -DEXTRA_CONF_FILE=wifi.conf

west flash --esp-device /dev/ttyUSB0
west espressif monitor
```

Không có `wifi.conf` vẫn build được, nhưng robot không vào mạng nên không điều
khiển được.

| File conf thêm | Tác dụng |
|---|---|
| `wifi.conf` | SSID, mật khẩu (không commit) |
| `debug.conf` | In mức dùng stack mỗi 30 s |
| `bringup.conf` | Chỉ thử phần cứng bằng shell; không chạy điều khiển, mạng |

Nhiều file: `-- "-DEXTRA_CONF_FILE=wifi.conf;debug.conf"`.

Web và âm thanh nằm trong firmware (không có bước `uploadfs`). Bảng tuyến lưu ở
`/lfs/tables.json`; lần chạy đầu được tạo từ `assets/tables.json`.

## Sử dụng

Mở `http://robot.local` hoặc IP in ra console. Giao diện, lệnh WebSocket và
`GET/PUT /api/tables` giống bản cũ.

Mã tuyến: chuỗi `L`/`R`/`S` kết thúc bằng `0`, tối đa 90 ký tự. Ngã rẽ là lúc
S1 và S5 cùng thấy line (sau khi S3 ở giữa line 200 ms). Tại ngã rẽ xe dừng 2 s
rồi: `S` chạy thẳng 0,5 s; `L`/`R` quay một bánh tới khi S3 bắt lại line (quá
1,5 s thì dừng hẳn); `0` là tới đích.

## Test trên máy

```bash
west build -p always -b native_sim/native/64 tests/logic -d build-logic
./build-logic/zephyr/zephyr.exe -stop_at=100
```

Thêm `-- -DCONFIG_ASAN=y -DCONFIG_UBSAN=y` để chạy với sanitizer. Test phủ PID,
mã tuyến, bảng điểm đến, JSON, giao thức web, vòng điều khiển, ngã rẽ, vật cản.
Không phủ driver phần cứng, WiFi và `web_server.c`.

## Kiểm tra trên board

**Kê xe để bánh không chạm đất** cho tới khi xong bước 3.

1. **Cảm biến và motor** (build với `bringup.conf`):

   | Lệnh | Mong đợi |
   |---|---|
   | `robot sensors` | S1..S5 đúng thứ tự trái → phải; trên line = 1 |
   | `robot motor 120 0 1000` | chỉ bánh trái quay tiến |
   | `robot motor 0 120 1000` | chỉ bánh phải quay tiến |
   | `robot motor -120 -120 1000` | hai bánh lùi |
   | `robot stop` | dừng ngay |

2. **Mạng** (build với `wifi.conf`): có IP, `ping robot.local`, mở web, đồng hồ
   TM1637 hiện giờ Việt Nam, nghe `welcome` khi vào mạng.
3. **Web:** START, STOP, E-STOP, lái tay (thả nút thì dừng; ngắt WiFi điện thoại
   thì dừng sau 0,6 s), chỉnh PID, thêm/sửa/lưu tuyến, tải lại trang nhiều lần,
   mở nhiều tab.
4. **Vật cản:** 5 cm dừng, 12 cm vẫn dừng, trên 15 cm chạy tiếp.
5. **Chạy thật:** bám line (AUTO), từng loại ngã rẽ `S`, `L`, `R`, `0`.
6. **Độ bền** (build thêm `debug.conf`): xem stack còn dư; theo dõi log
   `control loop stalled` (không nên xuất hiện, nhất là khi lưu bảng tuyến);
   theo dõi lỗi cấp phát bộ nhớ của WiFi.

## Khác bản Arduino

- Hostname DHCP là `robot` (cũ: `robot-line`); không quét, in danh sách WiFi.
- Âm thanh là WAV 16 kHz mono; chỉ có `welcome` (tên `.mp3` vẫn được nhận).
- Tối đa 4 client web (cũ: 8), 32 điểm đến.
- E-STOP/STOP vẫn được nhận khi hàng đợi lệnh đầy.
- Thêm an toàn: watchdog, cắt motor khi luồng điều khiển trễ > 100 ms, reset khi
  fault, dừng khi đọc cảm biến lỗi.
- `tables.json` được ghi qua file tạm rồi đổi tên.
- Web chỉ phục vụ `index.html`, `app.js`, `style.css`, `/api/tables`, `/ws`.

## Giới hạn đã biết

- Mất line (5 mắt = 0): xe **tiếp tục chạy** theo sai số cũ, như bản cũ.
- Cảm biến siêu âm hỏng hoặc rút dây: xe **không dừng**, như bản cũ.
- Mất WiFi ở chế độ AUTO/PATH: xe vẫn chạy; chỉ lái tay mới tự dừng.
- Lưu bảng tuyến ghi flash, có thể làm vòng điều khiển khựng. Chưa đo; nên lưu
  khi xe đang dừng.
- Web không có xác thực (như bản cũ): chỉ dùng trong mạng tin cậy.
- DRAM đã dùng khoảng 76 %; mức heap và stack thực tế chưa đo.
