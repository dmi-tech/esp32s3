/* Giao thức WebSocket: phải khớp web_control.cpp bản cũ */

#include <stdlib.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "destination_manager.h"
#include "events.h"
#include "fakes.h"
#include "web_protocol.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_reset();
}

ZTEST_SUITE(web, NULL, NULL, before, NULL, NULL);

static struct web_reply handle(const char *json)
{
	struct web_reply reply;

	web_handle_command(json, strlen(json), &reply);
	return reply;
}

static struct command expect_command(uint8_t type)
{
	struct command c;

	zassert_ok(k_msgq_get(&g_cmd_queue, &c, K_NO_WAIT));
	zassert_equal(c.type, type);
	return c;
}

static void expect_no_command(void)
{
	zassert_equal(k_msgq_num_used_get(&g_cmd_queue), 0);
}

ZTEST(web, test_simple_commands)
{
	struct command c;

	(void)handle("{\"cmd\":\"start\"}");
	(void)expect_command(CMD_START);
	(void)handle("{\"cmd\":\"estop\"}");
	(void)expect_command(CMD_ESTOP);
	(void)handle("{\"cmd\":\"drive\",\"dir\":\"left\"}");
	c = expect_command(CMD_DRIVE);
	zassert_str_equal(c.text, "left");
	(void)handle("{\"cmd\":\"drive\"}");
	c = expect_command(CMD_DRIVE);
	zassert_str_equal(c.text, "stop");
	(void)handle("{\"cmd\":\"play\"}");
	c = expect_command(CMD_PLAY);
	zassert_str_equal(c.text, "welcome.mp3");

	/* Lệnh không đổi trạng thái trực tiếp */
	zassert_equal(g_robot.mode, MODE_STOP);
}

ZTEST(web, test_ignores_bad_input)
{
	(void)handle("not json");
	(void)handle("{\"cmd\":\"fly\"}");
	(void)handle("{\"dir\":\"forward\"}");
	(void)handle("{\"cmd\":5}");
	(void)handle("");
	expect_no_command();
}

ZTEST(web, test_config_keeps_missing_fields)
{
	struct command c;

	(void)handle("{\"cmd\":\"config\",\"kp\":12.5,\"kd\":\"x\",\"base\":99.5}");
	c = expect_command(CMD_CONFIG);
	zassert_within(c.kp, 12.5f, 1e-6f);
	zassert_within(c.ki, 0.0f, 1e-6f);
	zassert_within(c.kd, 20.0f, 1e-6f);
	zassert_equal(c.base, 150);
}

static void expect_goto_rejected(const char *json, const char *reason)
{
	const struct web_reply r = handle(json);

	zassert_true(r.send_goto_result, "%s", json);
	zassert_false(r.ok, "%s", json);
	zassert_str_equal(r.message, reason, "%s", json);
	zassert_str_equal(g_robot.notice, reason);
	expect_no_command();
}

ZTEST(web, test_goto_validation)
{
	expect_goto_rejected("{\"cmd\":\"goto\"}", "Invalid destination ID");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":\"1\"}", "Invalid destination ID");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":1.5}", "Invalid destination ID");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":0}", "Invalid destination ID");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":7}",
			     "Destination not found; save or send a valid route");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":7,\"code\":\"\"}",
			     "Destination not found; save or send a valid route");
	expect_goto_rejected("{\"cmd\":\"goto\",\"id\":7,\"code\":\"LR\"}",
			     "code must end with 0");
}

ZTEST(web, test_goto_with_code_and_table)
{
	const char *table = "{\"4\":\"RSR0\"}";
	const char *error = NULL;
	struct web_reply r;
	struct command c;

	r = handle("{\"cmd\":\"goto\",\"id\":2,\"code\":\"L0\"}");
	zassert_true(r.ok);
	zassert_str_equal(r.message, "Route 2 (L0) queued");
	c = expect_command(CMD_GOTO);
	zassert_equal(c.value, 2);
	zassert_str_equal(c.text, "L0");

	zassert_ok(destinations_save_json(table, strlen(table), &error));
	r = handle("{\"cmd\":\"goto\",\"id\":4}");
	zassert_true(r.ok);
	c = expect_command(CMD_GOTO);
	zassert_str_equal(c.text, "RSR0");
}

ZTEST(web, test_full_queue)
{
	struct web_reply r;

	for (int i = 0; i < COMMAND_QUEUE_LEN; i++) {
		(void)handle("{\"cmd\":\"start\"}");
	}

	r = handle("{\"cmd\":\"goto\",\"id\":2,\"code\":\"L0\"}");
	zassert_false(r.ok);
	zassert_str_equal(r.message, "Robot command queue is full");
	zassert_str_equal(g_robot.notice, "Command queue full; route was not started");

	/* E-STOP vẫn phải tới được luồng điều khiển */
	(void)handle("{\"cmd\":\"estop\"}");
	zassert_equal(k_msgq_num_used_get(&g_cmd_queue), 1);
	(void)expect_command(CMD_ESTOP);
}

ZTEST(web, test_tables_reload)
{
	char code[ROUTE_CODE_MAX_LEN + 1];

	zassert_ok(tables_storage_write("{\"8\":\"S0\"}", 10));
	(void)handle("{\"cmd\":\"tables_reload\"}");
	zassert_true(destinations_lookup(8, code));
	zassert_str_equal(g_robot.notice, "Tables reloaded");
	zassert_true(event_take_config_request());
	expect_no_command();
}

ZTEST(web, test_json_output)
{
	char buf[768];

	zassert_true(web_config_json(buf, sizeof(buf), &g_robot) > 0);
	zassert_str_equal(buf, "{\"type\":\"config\",\"kp\":35.000,\"ki\":0.000,"
			       "\"kd\":20.000,\"base\":150,\"max\":255,\"on\":false,"
			       "\"mode\":\"stop\",\"dest\":0,\"dir\":\"stop\"}");

	g_robot.sensor[2] = 1;
	g_robot.error = -1.5f;
	robot_set_notice("say \"hi\"");
	zassert_true(web_telemetry_json(buf, sizeof(buf), &g_robot, "10.0.0.2", -40) > 0);
	zassert_str_equal(buf, "{\"type\":\"telemetry\",\"s\":[0,0,1,0,0],\"err\":-1.50,"
			       "\"pid\":0.00,\"l\":0,\"r\":0,\"full\":false,\"on\":false,"
			       "\"mode\":\"stop\",\"dir\":\"stop\",\"dest\":0,\"step\":0,"
			       "\"notice\":\"say \\\"hi\\\"\",\"rssi\":-40,"
			       "\"ip\":\"10.0.0.2\"}");

	zassert_true(web_event_json(buf, sizeof(buf), "[X] y") > 0);
	zassert_str_equal(buf, "{\"type\":\"event\",\"message\":\"[X] y\"}");
	zassert_true(web_goto_result_json(buf, sizeof(buf), true, "ok") > 0);
	zassert_str_equal(buf, "{\"type\":\"command_result\",\"cmd\":\"goto\",\"ok\":true,"
			       "\"message\":\"ok\"}");
	zassert_true(web_event_json(buf, 10, "x") < 0);
}

/* Trường hợp xấu nhất vẫn vừa bộ đệm gửi 768 byte của web_server.c */
ZTEST(web, test_worst_case_telemetry_fits)
{
	char buf[768];

	memset(g_robot.notice, '\x01', sizeof(g_robot.notice) - 1);
	memset(g_robot.drive_dir, '"', sizeof(g_robot.drive_dir) - 1);
	g_robot.error = -1.0e9f;
	g_robot.correction = -1.0e30f;

	zassert_true(web_telemetry_json(buf, sizeof(buf), &g_robot, "255.255.255.255",
					-127) > 0);
}

/* Sinh số giả ngẫu nhiên cố định để lỗi lặp lại được */
static uint32_t next_random(void)
{
	static uint32_t state = 0x2545F491U;

	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

/*
 * Đột biến các lệnh hợp lệ rồi đưa vào parser. Mỗi đầu vào nằm trong bộ đệm
 * đúng kích thước nên đọc lố sẽ bị AddressSanitizer bắt.
 */
ZTEST(web, test_mutated_input_is_safe)
{
	static const char *const seeds[] = {
		"{\"cmd\":\"goto\",\"id\":3,\"code\":\"LRS0\"}",
		"{\"cmd\":\"config\",\"kp\":1.5e2,\"ki\":-0.1,\"base\":120}",
		"{\"cmd\":\"drive\",\"dir\":\"a\\u00e9\\n\",\"x\":[{},[true,null]]}",
		"{\"1\":\"S0\",\"2\":\"LR0\"}",
	};
	static const char alphabet[] = "{}[]\":,\\u0123456789.eE+-tfn \x01";

	for (int round = 0; round < 40000; round++) {
		char work[96];
		const char *seed = seeds[next_random() % ARRAY_SIZE(seeds)];
		size_t len = strlen(seed);
		struct web_reply reply;
		const char *error = NULL;
		struct command c;

		memcpy(work, seed, len);
		for (uint32_t i = next_random() % 6U; i > 0U; i--) {
			work[next_random() % len] =
				alphabet[next_random() % (sizeof(alphabet) - 1)];
		}
		if (next_random() % 4U == 0U) {
			len = next_random() % (len + 1U);
		}

		char *exact = malloc(len == 0U ? 1U : len);

		zassert_not_null(exact);
		memcpy(exact, work, len);
		web_handle_command(exact, len, &reply);
		(void)destinations_save_json(exact, len, &error);
		free(exact);

		while (k_msgq_get(&g_cmd_queue, &c, K_NO_WAIT) == 0) {
			zassert_not_null(memchr(c.text, '\0', sizeof(c.text)));
		}
	}
}
