#include <math.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "audio.h"
#include "drive.h"
#include "events.h"
#include "line_pid.h"
#include "path_control.h"
#include "robot_logic.h"

LOG_MODULE_REGISTER(logic, LOG_LEVEL_INF);

static struct {
	bool obstacle_paused;
	uint32_t obstacle_pause_started_ms;
	bool junction_pattern_active;
	bool manual_timeout_logged;
	bool approach_slow_active;
	int approach_left;
	int approach_right;
} lg;

void robot_logic_reset(void)
{
	memset(&lg, 0, sizeof(lg));
	path_control_reset();
}

/* Phần chung của START và GOTO: bỏ hành động cũ, đặt lại tuyến */
static void begin_run(enum robot_mode mode, const char *path, int dest_id, uint32_t now_ms)
{
	path_cancel_action();
	pid_reset(&g_robot, now_ms);
	g_robot.enabled = true;
	g_robot.mode = mode;
	robot_copy_text(g_robot.drive_dir, sizeof(g_robot.drive_dir), "stop");
	robot_copy_text(g_robot.path, sizeof(g_robot.path), path);
	g_robot.path_idx = 0;
	g_robot.dest_id = dest_id;
	g_robot.turn = 0;
	g_robot.junction_armed = false;
	g_robot.centered_since_ms = 0;
}

void robot_apply_command(const struct command *c, uint32_t now_ms)
{
	robot_state_lock();

	switch (c->type) {
	case CMD_START:
		begin_run(MODE_AUTO, "", 0, now_ms);
		robot_set_notice("AUTO start");
		LOG_INF("[WEB] START (AUTO)");
		break;

	case CMD_GOTO:
		begin_run(MODE_PATH, c->text, c->value, now_ms);
		g_robot.junction_since_ms = 0;
		robot_set_noticef("PATH dest=%d code=%s", g_robot.dest_id, g_robot.path);
		LOG_INF("[WEB] GOTO %s", g_robot.notice);
		break;

	case CMD_STOP:
	case CMD_ESTOP:
		path_cancel_action();
		g_robot.enabled = false;
		g_robot.mode = MODE_STOP;
		robot_copy_text(g_robot.drive_dir, sizeof(g_robot.drive_dir), "stop");
		g_robot.turn = 0;
		g_robot.path[0] = '\0';
		g_robot.path_idx = 0;
		drive_halt();
		robot_set_notice(c->type == CMD_ESTOP ? "E-STOP" : "STOP");
		LOG_INF("[WEB] %s", c->type == CMD_ESTOP ? "E-STOP" : "STOP");
		break;

	case CMD_DRIVE:
		path_cancel_action();
		robot_copy_text(g_robot.drive_dir, sizeof(g_robot.drive_dir), c->text);

		if (strcmp(g_robot.drive_dir, "stop") == 0) {
			g_robot.enabled = false;
			g_robot.mode = MODE_STOP;
			drive_halt();
		} else {
			g_robot.enabled = true;
			g_robot.mode = MODE_MANUAL;
			g_robot.last_drive_cmd_ms = now_ms;
		}

		LOG_INF("[WEB] DRIVE %s", g_robot.drive_dir);
		break;

	case CMD_CONFIG:
		g_robot.kp = c->kp;
		g_robot.ki = c->ki;
		g_robot.kd = c->kd;
		g_robot.base_speed = c->base;
		LOG_INF("[WEB] PID Kp=%.3f Ki=%.3f Kd=%.3f base=%d", (double)g_robot.kp,
			(double)g_robot.ki, (double)g_robot.kd, g_robot.base_speed);
		break;

	case CMD_PLAY:
		audio_request(c->text);
		LOG_INF("[WEB] PLAY %s", c->text);
		break;

	default:
		break;
	}

	event_request_config();
	robot_state_unlock();
}

void robot_fail_stop(const char *why)
{
	robot_state_lock();
	path_cancel_action();
	g_robot.enabled = false;
	g_robot.mode = MODE_STOP;
	drive_halt();

	if (strcmp(g_robot.notice, why) != 0) {
		robot_set_notice(why);
		event_log("[SAFETY] %s; motors stopped", why);
		event_request_config();
	}

	robot_state_unlock();
}

static void handle_obstacle_transition(uint32_t now_ms)
{
	if (g_robot.obstacle_detected && !lg.obstacle_paused) {
		lg.obstacle_paused = true;
		lg.obstacle_pause_started_ms = now_ms;
		robot_set_noticef("OBSTACLE %.1f cm", (double)g_robot.obstacle_distance_cm);
		LOG_INF("[OBSTACLE] detected at %.1f cm; motors paused",
			(double)g_robot.obstacle_distance_cm);
	} else if (!g_robot.obstacle_detected && lg.obstacle_paused) {
		path_adjust_action_clock(now_ms - lg.obstacle_pause_started_ms);
		g_robot.junction_since_ms = 0;
		g_robot.centered_since_ms = 0;
		pid_reset(&g_robot, now_ms);
		lg.obstacle_paused = false;
		robot_set_notice("Obstacle cleared; resuming");
		LOG_INF("[OBSTACLE] clear; resuming previous mode");
	}
}

static void drive_approach_slow(void)
{
	drive_motors(lg.approach_left * JUNCTION_APPROACH_PERCENT / 100,
		     lg.approach_right * JUNCTION_APPROACH_PERCENT / 100);
}

/* Trả về true khi bộ dò ngã rẽ đã quyết định đầu ra motor cho chu kỳ này */
static bool junction_step(uint32_t now_ms)
{
	const uint8_t *s = g_robot.sensor;
	const bool pattern = s[0] == 1U && s[4] == 1U;
	const bool center_on = s[2] == 1U;

	if (pattern != lg.junction_pattern_active) {
		lg.junction_pattern_active = pattern;
		event_log("[JUNCTION] %s S=%d%d%d%d%d armed=%s",
			  pattern ? "PATTERN DETECTED" : "pattern cleared", s[0], s[1], s[2],
			  s[3], s[4], g_robot.junction_armed ? "yes" : "no");

		if (pattern && g_robot.junction_armed && !lg.approach_slow_active) {
			lg.approach_left = g_robot.left_speed;
			lg.approach_right = g_robot.right_speed;
			lg.approach_slow_active = true;
			drive_approach_slow();
			event_log("[JUNCTION] APPROACH SLOW to %d%% while confirming",
				  JUNCTION_APPROACH_PERCENT);
		}
	}

	if (pattern) {
		if (g_robot.junction_since_ms == 0U) {
			g_robot.junction_since_ms = now_ms;
		}
	} else {
		g_robot.junction_since_ms = 0;
		lg.approach_slow_active = false;
	}

	if (!pattern && center_on) {
		if (g_robot.centered_since_ms == 0U) {
			g_robot.centered_since_ms = now_ms;
		} else if (!g_robot.junction_armed &&
			   now_ms - g_robot.centered_since_ms >= CENTER_ARM_MS) {
			g_robot.junction_armed = true;
			event_log("[JUNCTION] detector ARMED");
		}
	} else {
		g_robot.centered_since_ms = 0;
	}

	if (!pattern || !g_robot.junction_armed) {
		return false;
	}

	if (g_robot.junction_since_ms != 0U &&
	    now_ms - g_robot.junction_since_ms >= JUNCTION_STABLE_MS) {
		g_robot.junction_armed = false;
		g_robot.junction_since_ms = 0;
		lg.approach_slow_active = false;
		path_handle_junction(s[0] != 0U, s[4] != 0U, now_ms);
		return true;
	}

	if (lg.approach_slow_active) {
		drive_approach_slow();
	}

	return true;
}

static void manual_step(uint32_t now_ms)
{
	if (now_ms - g_robot.last_drive_cmd_ms > DRIVE_TIMEOUT) {
		drive_halt();
		if (!lg.manual_timeout_logged) {
			LOG_INF("[MANUAL] command timeout; motors stopped");
			lg.manual_timeout_logged = true;
		}
		return;
	}

	apply_manual_drive();
	lg.manual_timeout_logged = false;
}

static void pid_step(uint32_t now_ms)
{
	g_robot.error = line_position(g_robot.sensor, g_robot.last_error);

	float correction = pid_calculate(&g_robot, g_robot.error, now_ms);

	if (!isfinite(correction)) {
		robot_fail_stop("PID output is not finite");
		return;
	}

	g_robot.correction = correction;

	const float limit = (float)g_robot.max_speed;
	const float base = (float)g_robot.base_speed;

	/* Kẹp ở dạng float để không ép kiểu một số ngoài miền int */
	drive_motors((int)CLAMP(base - correction, -limit, limit),
		     (int)CLAMP(base + correction, -limit, limit));
}

static void step_locked(uint32_t now_ms, const uint8_t sensor[5])
{
	struct command c;
	int active = 0;

	handle_obstacle_transition(now_ms);

	while (k_msgq_get(&g_cmd_queue, &c, K_NO_WAIT) == 0) {
		robot_apply_command(&c, now_ms);
	}

	path_update_destination_hold(now_ms);

	for (int i = 0; i < 5; i++) {
		g_robot.sensor[i] = sensor[i];
		active += sensor[i];
	}
	g_robot.full_line = active == 5;

	if (g_robot.obstacle_detected) {
		lg.approach_slow_active = false;
		drive_halt();
		return;
	}

	if (path_update_action(now_ms)) {
		return;
	}

	/* Hành động theo thời gian phải xong trước khi đọc ký tự tuyến kế tiếp */
	if (g_robot.mode == MODE_PATH && g_robot.turn == 0) {
		if (junction_step(now_ms)) {
			return;
		}
	} else if (lg.junction_pattern_active) {
		lg.junction_pattern_active = false;
		LOG_INF("[JUNCTION] pattern cleared (PATH detector inactive)");
	}

	if (g_robot.mode == MODE_STOP) {
		drive_halt();
		return;
	}

	if (g_robot.mode == MODE_MANUAL) {
		manual_step(now_ms);
		return;
	}

	/* Chỉ một mắt ngoài cùng thấy line: giữ nguyên lệnh motor trước đó */
	const bool outermost_only =
		active == 1 && (g_robot.sensor[0] == 1U || g_robot.sensor[4] == 1U);

	if (outermost_only && (g_robot.left_speed != 0 || g_robot.right_speed != 0)) {
		return;
	}

	pid_step(now_ms);
}

void robot_logic_step(uint32_t now_ms, const uint8_t sensor[5])
{
	robot_state_lock();
	step_locked(now_ms, sensor);
	robot_state_unlock();
}
