#pragma once

#include <stdint.h>

/* Cấu hình chân và đưa motor về trạng thái dừng. Gọi trước mọi thứ khác. */
int motor_control_init(void);

/* Chỉ xuất ra phần cứng, tốc độ -255..255. */
void motor_output(int left, int right);

/* Dừng phần cứng. Không đổi left_speed/right_speed (như bản cũ). */
void stop_motors(void);

/*
 * Chỉ kéo IN1..IN4 xuống LOW. Dùng được trong ISR và trong xử lý lỗi nghiêm
 * trọng (không đụng PWM vì driver PWM có dùng semaphore).
 */
void motor_emergency_cut(void);

int line_sensors_init(void);

/* sensor[i] = 1 khi mắt S(i+1) thấy line. Lỗi đọc trả về mã âm. */
int read_sensors(uint8_t sensor[5]);
