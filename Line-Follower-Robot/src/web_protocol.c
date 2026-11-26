#include <errno.h>
#include <math.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "destination_manager.h"
#include "events.h"
#include "json_flat.h"
#include "web_protocol.h"

LOG_MODULE_REGISTER(protocol, LOG_LEVEL_INF);

#define ESCAPED_LEN 256

/* Các trường có thể có trong một lệnh; type NULL = không có trường đó */
struct fields {
	struct json_flat_value cmd, dir, id, code, file, kp, ki, kd, base;
};

static void collect_field(const char *key, const struct json_flat_value *value, void *ctx)
{
	struct fields *f = ctx;
	const struct {
		const char *name;
		struct json_flat_value *slot;
	} map[] = {
		{"cmd", &f->cmd}, {"dir", &f->dir}, {"id", &f->id},
		{"code", &f->code}, {"file", &f->file}, {"kp", &f->kp},
		{"ki", &f->ki}, {"kd", &f->kd}, {"base", &f->base},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		if (strcmp(key, map[i].name) == 0) {
			*map[i].slot = *value;
			return;
		}
	}
}

static bool present(const struct json_flat_value *v)
{
	return v->raw != NULL;
}

/* Như doc[key] | fallback: thiếu hoặc sai kiểu thì dùng giá trị mặc định */
static void string_or(const struct json_flat_value *v, char *out, size_t size,
		      const char *fallback)
{
	char text[128];

	if (present(v) && json_flat_get_string(v, text, sizeof(text))) {
		robot_copy_text(out, size, text);
	} else {
		robot_copy_text(out, size, fallback);
	}
}

static void reject_goto(struct web_reply *reply, const char *reason)
{
	robot_set_notice(reason);
	reply->send_goto_result = true;
	reply->ok = false;
	robot_copy_text(reply->message, sizeof(reply->message), reason);
}

/* Điền id và mã tuyến đã kiểm tra vào c; false nếu đã từ chối */
static bool prepare_goto(const struct fields *f, struct command *c, struct web_reply *reply)
{
	char code[128] = "";
	const char *error = NULL;
	int id;

	if (!present(&f->id) || !json_flat_get_int(&f->id, &id) || id <= 0) {
		reject_goto(reply, "Invalid destination ID");
		return false;
	}

	if (present(&f->code) && f->code.type == JSON_FLAT_STRING) {
		if (!json_flat_get_string(&f->code, code, sizeof(code))) {
			error = "code too long";
		}
	} else if (!destinations_lookup(id, code)) {
		code[0] = '\0';
	}

	if (error == NULL && code[0] == '\0') {
		error = "Destination not found; save or send a valid route";
	} else if (error == NULL) {
		(void)route_path_valid(code, &error);
	}

	if (error != NULL) {
		reject_goto(reply, error);
		event_request_config();
		return false;
	}

	c->type = CMD_GOTO;
	c->value = id;
	robot_copy_text(c->text, sizeof(c->text), code);

	return true;
}

static void enqueue(const struct command *c, struct web_reply *reply)
{
	/*
	 * Giữ khoá để luồng điều khiển chưa xử lý lệnh trước khi notice "queued"
	 * được ghi (nếu không nó có thể đè lên notice "PATH ...").
	 */
	robot_state_lock();

	const int ret = k_msgq_put(&g_cmd_queue, c, K_NO_WAIT);

	if (c->type == CMD_GOTO) {
		reply->send_goto_result = true;
		reply->ok = ret == 0;

		if (ret == 0) {
			(void)snprintk(reply->message, sizeof(reply->message),
				       "Route %d (%.60s) queued", c->value, c->text);
			robot_set_notice(reply->message);
		} else {
			robot_set_notice("Command queue full; route was not started");
			robot_copy_text(reply->message, sizeof(reply->message),
					"Robot command queue is full");
		}
	} else if (ret != 0) {
		LOG_WRN("[WS] command queue full; command %u dropped", c->type);

		/* STOP và E-STOP không được phép bị bỏ vì hàng đợi đầy */
		if (c->type == CMD_STOP || c->type == CMD_ESTOP) {
			k_msgq_purge(&g_cmd_queue);
			(void)k_msgq_put(&g_cmd_queue, c, K_NO_WAIT);
		}
	}

	robot_state_unlock();
}

void web_handle_command(const char *text, size_t len, struct web_reply *reply)
{
	struct fields f = {0};
	struct command c = {0};
	char cmd[16];

	memset(reply, 0, sizeof(*reply));

	if (json_flat_parse(text, len, collect_field, &f) != JSON_FLAT_OK) {
		LOG_WRN("[WS] Invalid command JSON");
		return;
	}

	string_or(&f.cmd, cmd, sizeof(cmd), "");

	if (strcmp(cmd, "start") == 0) {
		c.type = CMD_START;
	} else if (strcmp(cmd, "stop") == 0) {
		c.type = CMD_STOP;
	} else if (strcmp(cmd, "estop") == 0) {
		c.type = CMD_ESTOP;
	} else if (strcmp(cmd, "drive") == 0) {
		c.type = CMD_DRIVE;
		string_or(&f.dir, c.text, sizeof(c.text), "stop");
	} else if (strcmp(cmd, "goto") == 0) {
		if (!prepare_goto(&f, &c, reply)) {
			return;
		}
	} else if (strcmp(cmd, "play") == 0) {
		c.type = CMD_PLAY;
		string_or(&f.file, c.text, sizeof(c.text), "welcome.mp3");
	} else if (strcmp(cmd, "config") == 0) {
		c.type = CMD_CONFIG;

		robot_state_lock();
		c.kp = g_robot.kp;
		c.ki = g_robot.ki;
		c.kd = g_robot.kd;
		c.base = g_robot.base_speed;
		robot_state_unlock();

		/* Trường thiếu hoặc sai kiểu giữ giá trị hiện tại */
		if (present(&f.kp)) {
			(void)json_flat_get_float(&f.kp, &c.kp);
		}
		if (present(&f.ki)) {
			(void)json_flat_get_float(&f.ki, &c.ki);
		}
		if (present(&f.kd)) {
			(void)json_flat_get_float(&f.kd, &c.kd);
		}
		if (present(&f.base)) {
			(void)json_flat_get_int(&f.base, &c.base);
		}
	} else if (strcmp(cmd, "tables_reload") == 0) {
		(void)destinations_load();
		robot_set_notice("Tables reloaded");
		event_request_config();
		return;
	}

	if (c.type != CMD_NONE) {
		enqueue(&c, reply);
	}
}

static int checked_length(int written, size_t size)
{
	return (written < 0 || (size_t)written >= size) ? -ENOSPC : written;
}

static double finite_or_zero(float value)
{
	return isfinite(value) ? (double)value : 0.0;
}

int web_config_json(char *buf, size_t size, const struct robot_state *s)
{
	char dir[ESCAPED_LEN];

	json_flat_escape(dir, sizeof(dir), s->drive_dir);

	return checked_length(
		snprintk(buf, size,
			 "{\"type\":\"config\",\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f,"
			 "\"base\":%d,\"max\":%d,\"on\":%s,\"mode\":\"%s\",\"dest\":%d,"
			 "\"dir\":\"%s\"}",
			 finite_or_zero(s->kp), finite_or_zero(s->ki), finite_or_zero(s->kd),
			 s->base_speed, s->max_speed, s->enabled ? "true" : "false",
			 robot_mode_name(s->mode), s->dest_id, dir),
		size);
}

int web_telemetry_json(char *buf, size_t size, const struct robot_state *s, const char *ip,
		       int rssi)
{
	char dir[ESCAPED_LEN];
	char notice[ESCAPED_LEN];

	json_flat_escape(dir, sizeof(dir), s->drive_dir);
	json_flat_escape(notice, sizeof(notice), s->notice);

	return checked_length(
		snprintk(buf, size,
			 "{\"type\":\"telemetry\",\"s\":[%u,%u,%u,%u,%u],\"err\":%.2f,"
			 "\"pid\":%.2f,\"l\":%d,\"r\":%d,\"full\":%s,\"on\":%s,"
			 "\"mode\":\"%s\",\"dir\":\"%s\",\"dest\":%d,\"step\":%u,"
			 "\"notice\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"}",
			 s->sensor[0], s->sensor[1], s->sensor[2], s->sensor[3], s->sensor[4],
			 finite_or_zero(s->error), finite_or_zero(s->correction), s->left_speed,
			 s->right_speed, s->full_line ? "true" : "false",
			 s->enabled ? "true" : "false", robot_mode_name(s->mode), dir,
			 s->dest_id, (unsigned int)s->path_idx, notice, rssi, ip),
		size);
}

int web_event_json(char *buf, size_t size, const char *message)
{
	char escaped[ESCAPED_LEN];

	json_flat_escape(escaped, sizeof(escaped), message);

	return checked_length(
		snprintk(buf, size, "{\"type\":\"event\",\"message\":\"%s\"}", escaped), size);
}

int web_goto_result_json(char *buf, size_t size, bool ok, const char *message)
{
	char escaped[ESCAPED_LEN];

	json_flat_escape(escaped, sizeof(escaped), message);

	return checked_length(snprintk(buf, size,
				       "{\"type\":\"command_result\",\"cmd\":\"goto\","
				       "\"ok\":%s,\"message\":\"%s\"}",
				       ok ? "true" : "false", escaped),
			      size);
}
