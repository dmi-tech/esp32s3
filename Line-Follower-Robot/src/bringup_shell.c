/*
 * Lệnh thử phần cứng (chỉ có khi build với bringup.conf).
 * Mỗi lệnh motor tự dừng sau tối đa BRINGUP_MAX_MS.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "motor_control.h"
#include "robot_config.h"

#define BRINGUP_MAX_MS 3000L
#define BRINGUP_SAMPLES 50
#define BRINGUP_SAMPLE_MS 200

static void motor_stop_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	stop_motors();
}

static K_WORK_DELAYABLE_DEFINE(motor_stop_work, motor_stop_work_handler);

static int parse_long(const char *text, long min, long max, long *out)
{
	char *end;
	const long value = strtol(text, &end, 10);

	if (end == text || *end != '\0' || value < min || value > max) {
		return -EINVAL;
	}

	*out = value;
	return 0;
}

static int cmd_sensors(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < BRINGUP_SAMPLES; i++) {
		uint8_t s[5];
		const int ret = read_sensors(s);

		if (ret != 0) {
			shell_error(sh, "read failed: %d", ret);
			return ret;
		}

		shell_print(sh, "S1..S5 = %u %u %u %u %u", s[0], s[1], s[2], s[3], s[4]);
		k_msleep(BRINGUP_SAMPLE_MS);
	}

	return 0;
}

static int cmd_motor(const struct shell *sh, size_t argc, char **argv)
{
	long left;
	long right;
	long duration;

	ARG_UNUSED(argc);

	if (parse_long(argv[1], -MOTOR_SPEED_LIMIT, MOTOR_SPEED_LIMIT, &left) != 0 ||
	    parse_long(argv[2], -MOTOR_SPEED_LIMIT, MOTOR_SPEED_LIMIT, &right) != 0 ||
	    parse_long(argv[3], 1, BRINGUP_MAX_MS, &duration) != 0) {
		shell_error(sh, "left/right: -255..255, ms: 1..%ld", BRINGUP_MAX_MS);
		return -EINVAL;
	}

	/*
	 * Phải xuất motor trước rồi mới hẹn giờ dừng. Làm ngược lại thì work
	 * (ưu tiên cao hơn shell) có thể dừng xong trước khi motor được bật.
	 */
	motor_output((int)left, (int)right);
	if (k_work_reschedule(&motor_stop_work, K_MSEC(duration)) < 0) {
		stop_motors();
		shell_error(sh, "cannot schedule stop");
		return -EIO;
	}

	shell_print(sh, "L=%ld R=%ld trong %ld ms", left, right, duration);

	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	stop_motors();
	shell_print(sh, "stopped");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	robot_cmds, SHELL_CMD(sensors, NULL, "In S1..S5 trong 10 s", cmd_sensors),
	SHELL_CMD_ARG(motor, NULL, "<left> <right> <ms>", cmd_motor, 4, 0),
	SHELL_CMD(stop, NULL, "Dừng motor", cmd_stop), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(robot, &robot_cmds, "Thử phần cứng robot", NULL);
