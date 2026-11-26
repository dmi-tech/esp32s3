# Lỗi đã gặp khi port sang Zephyr

Chỉ ghi lỗi thật đã mắc: lỗi gì, nguyên nhân, cách tránh.

## 1. Hẹn giờ dừng motor trước khi bật motor (`bringup_shell.c`, 2026-11-26)
- **Lỗi:** gọi `k_work_reschedule()` rồi mới `motor_output()`. System workqueue có
  ưu tiên cao hơn shell, nên với thời gian ngắn, lệnh dừng có thể chạy trước và
  motor quay không bao giờ dừng. Phát hiện khi tự rà code, chưa chạy trên board.
- **Cách tránh:** bật đầu ra trước, hẹn giờ tắt sau, và tắt ngay nếu hẹn giờ lỗi.

## 2. Frame WebSocket đến nhiều mảnh bị mất đầu (`web_server.c`)
- **Lỗi:** coi `type`/`remaining` của `websocket_recv_msg()` luôn được điền. Khi
  frame mới đến một phần, hàm trả dữ liệu nhưng để cả hai bằng 0, nên phần đầu
  bị vứt đi ("Invalid command JSON").
- **Cách tránh:** gom dữ liệu khi `type == 0`. Phát hiện bằng mô phỏng web server
  trên `native_sim`; unit test không phủ lớp socket.

## 3. Kết luận sai về lỗi rò socket khi nhận CLOSE (`web_server.c`)
- **Lỗi:** đọc mã thư viện rồi kết luận mọi frame CLOSE làm rò fd. Thực tế thư
  viện chỉ tự huỷ khi `message_type == CLOSE` đúng nguyên giá trị; CLOSE hợp lệ
  có thêm cờ FINAL nên **không** được xử lý, và code đã bỏ qua nó (không trả lời,
  không đóng). Test đóng/mở lặp lại ban đầu vẫn đạt vì client tự cắt TCP.
- **Cách tránh:** làm phép đối chứng (bỏ bản sửa, test phải trượt) trước khi tin
  vào một phân tích; test đóng kết nối phải đợi server trả CLOSE và đóng TCP.

## 4. Xoá cả ô client bằng `memset` (`web_server.c`)
- **Lỗi:** `memset` làm `sock = 0` (trông như ô đang dùng) trước khi gán fd thật.
- **Cách tránh:** gán từng trường, ghi `sock` sau cùng.

## 5. Lỗi biên dịch
- Tham số macro tên `type` thay luôn tên trường `.type` (`STATIC_RESOURCE`).
- Đoán tên API: đúng là `auxdisplay_brightness_set()`.
- Biến `lfs_mount` trùng tên hàm của thư viện LittleFS.
- `#include "x.inc"` đã có dấu phẩy cuối, không thêm `,` trước byte kết chuỗi.
- `snprintk("%s")` để cắt chuỗi gây `-Wformat-truncation`: dùng
  `robot_copy_text()`.
- `strnlen` không được khai báo khi build `native_sim`.

## Đặc điểm Zephyr cần nhớ (tại bản đã pin)
- Mỗi resource WebSocket chỉ khai báo một bộ đệm nhận, thư viện giữ dữ liệu đọc
  dở trong đó: phải đổi bộ đệm cho từng client. (Theo đọc mã.)
- Frame CLOSE hợp lệ không được thư viện trả lời: ứng dụng phải gửi CLOSE và
  đóng. CLOSE thiếu FIN thì thư viện huỷ context nhưng không đóng fd.
- Driver I2S ESP32 chỉ nhận 2 kênh; `I2S_TRIGGER_DROP` không trả block đang
  truyền về slab. (Theo đọc mã.)
- Board `esp32s3_devkitc` bật sẵn `spi2` trên GPIO10..13 và dùng bảng phân vùng
  4M dù khai báo flash 8 MB.
