#include "drive.h"
#include "line_pid.h"
#include "motor_control.h"
#include "robot_state.h"

void drive_motors(int left, int right)
{
	motor_output(left, right);

	robot_state_lock();
	g_robot.left_speed = left;
	g_robot.right_speed = right;
	robot_state_unlock();
}

void drive_halt(void)
{
	stop_motors();

	robot_state_lock();
	g_robot.left_speed = 0;
	g_robot.right_speed = 0;
	robot_state_unlock();
}

void apply_manual_drive(void)
{
	int left;
	int right;

	robot_state_lock();
	manual_drive_speeds(g_robot.drive_dir, g_robot.base_speed, g_robot.max_speed, &left,
			    &right);
	drive_motors(left, right);
	robot_state_unlock();
}
