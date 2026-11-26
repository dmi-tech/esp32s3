#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "audio.h"
#include "drive.h"
#include "events.h"
#include "line_pid.h"
#include "path_control.h"
#include "robot_state.h"

LOG_MODULE_REGISTER(path, LOG_LEVEL_INF);

enum action_phase {
	ACTION_NONE,
	ACTION_JUNCTION_STOP,
	ACTION_EXECUTE,
};

enum turn_sensor_phase {
	TURN_WAIT_CENTER_LINE,
	TURN_WAIT_CENTER_CLEAR,
	TURN_WAIT_CENTER_REACQUIRE,
};

static struct {
	bool hold_active;
	uint32_t hold_started_ms;
	enum action_phase phase;
	char pending;
	uint32_t action_started_ms;
	uint32_t candidate_since_ms;
	enum turn_sensor_phase turn_phase;
} pc;

static void clear_action(void)
{
	pc.phase = ACTION_NONE;
	pc.pending = '\0';
	pc.candidate_since_ms = 0;
	pc.turn_phase = TURN_WAIT_CENTER_LINE;
	g_robot.turn = 0;
}

static void clear_junction_detector(void)
{
	g_robot.junction_armed = false;
	g_robot.junction_since_ms = 0;
	g_robot.centered_since_ms = 0;
}

static void complete_action(uint32_t now_ms)
{
	clear_action();
	clear_junction_detector();
	pid_reset(&g_robot, now_ms);
	robot_set_notice("PATH action complete; following line");
	event_log("[JUNCTION] ACTION COMPLETE; resume line following");
}

static void stop_on_turn_timeout(void)
{
	drive_halt();
	g_robot.enabled = false;
	g_robot.mode = MODE_STOP;
	clear_action();
	clear_junction_detector();

	robot_set_notice("PATH turn timeout; center sensor did not reacquire line");
	event_log("[JUNCTION] TURN TIMEOUT; S3 did not reacquire line, stopped safely");
	event_request_config();
}

void path_control_reset(void)
{
	memset(&pc, 0, sizeof(pc));
}

void path_arrived(const char *why)
{
	drive_halt();
	clear_action();
	g_robot.enabled = false;
	g_robot.mode = MODE_STOP;

	robot_set_noticef("ARRIVED dest=%d (%s)", g_robot.dest_id, why);
	LOG_INF("%s", g_robot.notice);

	event_request_config();
}

void path_handle_junction(bool left_on, bool right_on, uint32_t now_ms)
{
	if (g_robot.path_idx >= strlen(g_robot.path)) {
		path_arrived("no more code");
		return;
	}

	const char code = g_robot.path[g_robot.path_idx];

	event_log("[JUNCTION] >>> CONFIRMED <<< idx=%u pattern=%d%d%d%d%d code=%c L=%d R=%d",
		  (unsigned int)g_robot.path_idx, g_robot.sensor[0], g_robot.sensor[1],
		  g_robot.sensor[2], g_robot.sensor[3], g_robot.sensor[4], code, left_on,
		  right_on);

	if (code == '0') {
		path_arrived("destination");
		pc.hold_active = true;
		pc.hold_started_ms = now_ms;
		audio_request("welcome.mp3");
		event_log("[PATH] destination reached; stopped and playing welcome.mp3");
		return;
	}

	g_robot.path_idx++;
	pc.pending = code;
	pc.phase = ACTION_JUNCTION_STOP;
	pc.action_started_ms = now_ms;
	pc.candidate_since_ms = 0;
	pc.turn_phase = TURN_WAIT_CENTER_LINE;
	g_robot.turn = code == 'L' ? -1 : (code == 'R' ? 1 : 0);
	drive_halt();
	robot_set_notice("Junction confirmed; waiting 2 s");
	event_log("[JUNCTION] STOP for %u ms; next action=%c", JUNCTION_STOP_MS, code);
}

void path_update_destination_hold(uint32_t now_ms)
{
	if (!pc.hold_active) {
		return;
	}

	if (g_robot.mode != MODE_STOP || g_robot.path[0] == '\0') {
		pc.hold_active = false;
		return;
	}

	if (now_ms - pc.hold_started_ms >= DESTINATION_HOLD_MS) {
		pc.hold_active = false;
		LOG_INF("[PATH] destination hold complete; remaining stopped");
	}
}

/*
 * Rẽ L/R: S3 phải qua ba pha, mỗi pha ổn định ROUTE_TURN_SENSOR_STABLE_MS.
 * Trả về true khi đã bắt lại line.
 */
static bool turn_sensor_step(uint32_t now_ms)
{
	const bool center_on = g_robot.sensor[2] == 1U;
	const bool expected = pc.turn_phase == TURN_WAIT_CENTER_CLEAR ? !center_on : center_on;

	if (!expected) {
		pc.candidate_since_ms = 0;
		return false;
	}

	if (pc.candidate_since_ms == 0U) {
		pc.candidate_since_ms = now_ms;
		return false;
	}

	if (now_ms - pc.candidate_since_ms < ROUTE_TURN_SENSOR_STABLE_MS) {
		return false;
	}

	pc.candidate_since_ms = 0;

	switch (pc.turn_phase) {
	case TURN_WAIT_CENTER_LINE:
		pc.turn_phase = TURN_WAIT_CENTER_CLEAR;
		event_log("[JUNCTION] S3 on line; waiting for stable departure");
		return false;
	case TURN_WAIT_CENTER_CLEAR:
		pc.turn_phase = TURN_WAIT_CENTER_REACQUIRE;
		event_log("[JUNCTION] S3 left line; waiting to reacquire");
		return false;
	default:
		event_log("[JUNCTION] S3 reacquired line; turn complete after %u ms",
			  (unsigned int)(now_ms - pc.action_started_ms));
		return true;
	}
}

bool path_update_action(uint32_t now_ms)
{
	if (pc.phase == ACTION_NONE) {
		return false;
	}

	if (g_robot.mode != MODE_PATH) {
		path_cancel_action();
		return false;
	}

	if (pc.phase == ACTION_JUNCTION_STOP) {
		drive_halt();

		if (now_ms - pc.action_started_ms < JUNCTION_STOP_MS) {
			return true;
		}

		pc.phase = ACTION_EXECUTE;
		pc.action_started_ms = now_ms;
		pc.candidate_since_ms = 0;
		pc.turn_phase = TURN_WAIT_CENTER_LINE;

		if (pc.pending == 'L' || pc.pending == 'R') {
			event_log("[JUNCTION] STOP COMPLETE; ACTION START code=%c until S3 "
				  "reacquires (timeout=%u ms)",
				  pc.pending, ROUTE_TURN_TIMEOUT_MS);
		} else {
			event_log("[JUNCTION] STOP COMPLETE; ACTION START code=%c duration=%u ms",
				  pc.pending, ROUTE_ACTION_MS);
		}
	}

	const int speed = CLAMP(g_robot.base_speed, 0, g_robot.max_speed);

	if (pc.pending == 'L' || pc.pending == 'R') {
		if (turn_sensor_step(now_ms)) {
			complete_action(now_ms);
			return true;
		}

		if (now_ms - pc.action_started_ms >= ROUTE_TURN_TIMEOUT_MS) {
			stop_on_turn_timeout();
			return true;
		}

		if (pc.pending == 'L') {
			drive_motors(0, speed);
		} else {
			drive_motors(speed, 0);
		}
		return true;
	}

	if (now_ms - pc.action_started_ms < ROUTE_ACTION_MS) {
		drive_motors(speed, speed);
		return true;
	}

	complete_action(now_ms);
	return true;
}

void path_adjust_action_clock(uint32_t pause_ms)
{
	if (pc.phase != ACTION_NONE) {
		pc.action_started_ms += pause_ms;
	}
}

void path_cancel_action(void)
{
	clear_action();
}
