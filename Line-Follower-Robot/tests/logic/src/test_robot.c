/*
 * Vòng điều khiển và máy trạng thái ngã rẽ. Thời gian giả, mỗi bước 5 ms
 * như LINE_LOOP_DELAY_MS.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "robot_logic.h"

static const uint8_t CENTER[5] = {0, 0, 1, 0, 0};
static const uint8_t JUNCTION[5] = {1, 1, 1, 1, 1};
static const uint8_t SIDES[5] = {1, 0, 0, 0, 1};
static const uint8_t NONE[5] = {0, 0, 0, 0, 0};
static const uint8_t LEFT_ONLY[5] = {1, 0, 0, 0, 0};
static const uint8_t SLIGHT_RIGHT[5] = {0, 0, 0, 1, 0};

static uint32_t now;

static void run(const uint8_t sensor[5], uint32_t duration_ms)
{
	for (uint32_t t = 0; t < duration_ms; t += LINE_LOOP_DELAY_MS) {
		now += LINE_LOOP_DELAY_MS;
		robot_logic_step(now, sensor);
	}
}

static void send(uint8_t type, const char *text, int value)
{
	struct command c = {.type = type, .value = value};

	robot_copy_text(c.text, sizeof(c.text), text);
	zassert_ok(k_msgq_put(&g_cmd_queue, &c, K_NO_WAIT));
}

static void start_route(const char *code)
{
	send(CMD_GOTO, code, 1);
	run(CENTER, CENTER_ARM_MS + 20);
	zassert_true(g_robot.junction_armed);
}

/* Đi qua ngã rẽ cho tới khi được xác nhận */
static void hit_junction(void)
{
	run(JUNCTION, JUNCTION_STABLE_MS + 10);
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_reset();
	now = 10000;
}

ZTEST_SUITE(robot, NULL, NULL, before, NULL, NULL);

ZTEST(robot, test_stop_mode_keeps_motors_off)
{
	run(CENTER, 100);
	zassert_equal(fake.left, 0);
	zassert_equal(fake.right, 0);
}

ZTEST(robot, test_auto_follows_line)
{
	send(CMD_START, "", 0);
	run(CENTER, 50);
	zassert_equal(g_robot.mode, MODE_AUTO);
	zassert_equal(fake.left, 150);
	zassert_equal(fake.right, 150);

	/* Ổn định ở sai số +1: correction = 35 */
	run(SLIGHT_RIGHT, 50);
	zassert_equal(fake.left, 115);
	zassert_equal(fake.right, 185);
}

ZTEST(robot, test_outer_sensor_only_keeps_previous_output)
{
	send(CMD_START, "", 0);
	run(CENTER, 50);
	run(LEFT_ONLY, 50);
	zassert_equal(fake.left, 150);
	zassert_equal(fake.right, 150);
}

ZTEST(robot, test_lost_line_keeps_driving_like_original)
{
	send(CMD_START, "", 0);
	run(SLIGHT_RIGHT, 50);
	run(NONE, 500);
	zassert_equal(g_robot.mode, MODE_AUTO);
	zassert_equal(fake.left, 115);
	zassert_equal(fake.right, 185);
}

ZTEST(robot, test_manual_drive_times_out)
{
	send(CMD_DRIVE, "forward", 0);
	run(CENTER, DRIVE_TIMEOUT);
	zassert_equal(fake.left, 150);
	run(CENTER, 20);
	zassert_equal(fake.left, 0);
	zassert_equal(fake.right, 0);

	send(CMD_DRIVE, "back", 0);
	run(CENTER, 10);
	zassert_equal(fake.left, -150);
	send(CMD_DRIVE, "stop", 0);
	run(CENTER, 10);
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_equal(fake.left, 0);
}

ZTEST(robot, test_estop_stops_immediately)
{
	send(CMD_START, "", 0);
	run(CENTER, 50);
	send(CMD_ESTOP, "", 0);
	run(CENTER, 5);
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_false(g_robot.enabled);
	zassert_equal(fake.left, 0);
	zassert_str_equal(g_robot.notice, "E-STOP");
}

ZTEST(robot, test_junction_needs_arming)
{
	send(CMD_GOTO, "L0", 1);
	run(JUNCTION, 200);
	zassert_equal(g_robot.path_idx, 0);
}

ZTEST(robot, test_junction_approach_slows_to_half)
{
	start_route("S0");
	zassert_equal(fake.left, 150);
	run(JUNCTION, 10);
	zassert_equal(fake.left, 75);
	zassert_equal(fake.right, 75);
	zassert_equal(g_robot.path_idx, 0);
}

ZTEST(robot, test_junction_glitch_is_ignored)
{
	start_route("L0");
	run(JUNCTION, JUNCTION_STABLE_MS - 10);
	run(CENTER, 20);
	zassert_equal(g_robot.path_idx, 0);
	zassert_equal(fake.left, 150);
}

ZTEST(robot, test_straight_action)
{
	start_route("S0");
	hit_junction();
	zassert_equal(g_robot.path_idx, 1);
	zassert_equal(fake.left, 0);

	run(JUNCTION, JUNCTION_STOP_MS - 20);
	zassert_equal(fake.left, 0, "vẫn đang dừng 2 s");
	run(JUNCTION, 40);
	zassert_equal(fake.left, 150);
	zassert_equal(fake.right, 150);
	run(JUNCTION, ROUTE_ACTION_MS);
	zassert_true(event_seen("ACTION COMPLETE"));
	zassert_false(g_robot.junction_armed);
	zassert_equal(g_robot.mode, MODE_PATH);
}

ZTEST(robot, test_left_turn_waits_for_center_three_phases)
{
	start_route("L0");
	hit_junction();
	run(JUNCTION, JUNCTION_STOP_MS);

	run(CENTER, 50); /* S3 trên line */
	zassert_equal(fake.left, 0);
	zassert_equal(fake.right, 150);
	run(NONE, 100); /* rời line */
	zassert_equal(g_robot.turn, -1);
	run(CENTER, 50); /* bắt lại */
	zassert_true(event_seen("turn complete"));
	zassert_equal(g_robot.turn, 0);
	zassert_equal(g_robot.mode, MODE_PATH);
}

ZTEST(robot, test_right_turn_drives_left_wheel)
{
	start_route("R0");
	hit_junction();
	run(JUNCTION, JUNCTION_STOP_MS);
	run(NONE, 20);
	zassert_equal(fake.left, 150);
	zassert_equal(fake.right, 0);
}

ZTEST(robot, test_turn_timeout_stops_robot)
{
	start_route("R0");
	hit_junction();
	run(JUNCTION, JUNCTION_STOP_MS);
	run(NONE, ROUTE_TURN_TIMEOUT_MS + 20);
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_false(g_robot.enabled);
	zassert_equal(fake.left, 0);
	zassert_equal(fake.right, 0);
	zassert_true(event_seen("TURN TIMEOUT"));
	run(CENTER, 100);
	zassert_equal(fake.left, 0);
}

ZTEST(robot, test_destination_reached)
{
	start_route("0");
	hit_junction();
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_equal(fake.left, 0);
	zassert_equal(fake.audio_requests, 1);
	zassert_str_equal(fake.audio_file, "welcome.mp3");
	zassert_str_equal(g_robot.notice, "ARRIVED dest=1 (destination)");
}

ZTEST(robot, test_full_route)
{
	start_route("S0");
	hit_junction();
	run(JUNCTION, JUNCTION_STOP_MS + ROUTE_ACTION_MS + 20);
	run(CENTER, CENTER_ARM_MS + 20);
	run(SIDES, JUNCTION_STABLE_MS + 10);
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_equal(fake.audio_requests, 1);
}

ZTEST(robot, test_obstacle_stops_and_extends_action)
{
	start_route("S0");
	hit_junction();
	run(JUNCTION, JUNCTION_STOP_MS + 100); /* đang chạy thẳng */
	zassert_equal(fake.left, 150);

	g_robot.obstacle_detected = true;
	g_robot.obstacle_distance_cm = 5.0f;
	run(JUNCTION, 3000);
	zassert_equal(fake.left, 0);
	zassert_equal(fake.right, 0);
	zassert_str_equal(g_robot.notice, "OBSTACLE 5.0 cm");

	g_robot.obstacle_detected = false;
	run(JUNCTION, 100);
	zassert_equal(fake.left, 150, "thời gian tạm dừng không được tính");
	zassert_false(event_seen("ACTION COMPLETE"));
	run(JUNCTION, ROUTE_ACTION_MS);
	zassert_true(event_seen("ACTION COMPLETE"));
}

ZTEST(robot, test_obstacle_blocks_every_mode)
{
	g_robot.obstacle_detected = true;
	send(CMD_DRIVE, "forward", 0);
	run(CENTER, 50);
	zassert_equal(fake.left, 0);
	send(CMD_START, "", 0);
	run(CENTER, 50);
	zassert_equal(fake.left, 0);
}

ZTEST(robot, test_new_command_cancels_junction_action)
{
	start_route("L0");
	hit_junction();
	send(CMD_START, "", 0);
	run(CENTER, 10);
	zassert_equal(g_robot.mode, MODE_AUTO);
	zassert_equal(fake.left, 150);
	zassert_equal(g_robot.turn, 0);
}

ZTEST(robot, test_fail_stop)
{
	send(CMD_START, "", 0);
	run(CENTER, 50);
	robot_fail_stop("line sensor read failed");
	zassert_equal(g_robot.mode, MODE_STOP);
	zassert_equal(fake.left, 0);
}
