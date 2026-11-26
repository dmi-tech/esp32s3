#pragma once

/* motor_output() và ghi lại left_speed/right_speed trong g_robot. */
void drive_motors(int left, int right);

/* Dừng motor và đặt left_speed = right_speed = 0. */
void drive_halt(void);

/* Lái tay theo g_robot.drive_dir */
void apply_manual_drive(void);
