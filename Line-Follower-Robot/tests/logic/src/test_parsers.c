/* PID, mã tuyến, bảng điểm đến, JSON, vật cản, WAV, đồng hồ */

#include <string.h>

#include <zephyr/ztest.h>

#include "clock_format.h"
#include "destination_manager.h"
#include "fakes.h"
#include "json_flat.h"
#include "line_pid.h"
#include "obstacle_filter.h"
#include "wav.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_reset();
}

ZTEST_SUITE(parsers, NULL, NULL, before, NULL, NULL);

ZTEST(parsers, test_line_position)
{
	const uint8_t center[5] = {0, 0, 1, 0, 0};
	const uint8_t left[5] = {1, 1, 0, 0, 0};
	const uint8_t right[5] = {0, 0, 0, 0, 1};
	const uint8_t none[5] = {0};

	zassert_within(line_position(center, 9.0f), 0.0f, 1e-6f);
	zassert_within(line_position(left, 0.0f), -1.5f, 1e-6f);
	zassert_within(line_position(right, 0.0f), 2.0f, 1e-6f);
	/* Mất line: giữ sai số trước đó */
	zassert_within(line_position(none, -1.5f), -1.5f, 1e-6f);
}

ZTEST(parsers, test_pid)
{
	g_robot.ki = 1.0f;
	pid_reset(&g_robot, 1000);

	/* dt = 0,01 s: P = 35, I = 0,01, D = 20 * 1 / 0,01 */
	zassert_within(pid_calculate(&g_robot, 1.0f, 1010), 35.0f + 0.01f + 2000.0f, 0.01f);
	/* Cùng thời điểm: dt bị thay bằng 1 ms, đạo hàm 0 */
	zassert_within(pid_calculate(&g_robot, 1.0f, 1010), 35.0f + 0.011f, 0.01f);
	/* Tích phân bị kẹp ở 10 */
	(void)pid_calculate(&g_robot, 2.0f, 61010);
	zassert_within(g_robot.integral, 10.0f, 1e-5f);
}

ZTEST(parsers, test_manual_drive)
{
	int l;
	int r;

	manual_drive_speeds("forward_left", 150, 255, &l, &r);
	zassert_equal(l, 75);
	zassert_equal(r, 150);
	manual_drive_speeds("back_right", 999, 255, &l, &r);
	zassert_equal(l, -255);
	zassert_equal(r, -127);
	manual_drive_speeds("forward", -5, 255, &l, &r);
	zassert_equal(l, 0);
	manual_drive_speeds("jump", 150, 255, &l, &r);
	zassert_equal(l, 0);
	zassert_equal(r, 0);
}

ZTEST(parsers, test_route_validation)
{
	const char *error = NULL;
	char longest[92];

	zassert_true(route_path_valid("0", &error));
	zassert_true(route_path_valid("LRLSLR0", &error));

	memset(longest, 'S', sizeof(longest));
	longest[89] = '0';
	longest[90] = '\0';
	zassert_true(route_path_valid(longest, &error));
	longest[89] = 'S';
	longest[90] = '0';
	longest[91] = '\0';
	zassert_false(route_path_valid(longest, &error));
	zassert_str_equal(error, "code too long");

	zassert_false(route_path_valid("", &error));
	zassert_str_equal(error, "empty code");
	zassert_false(route_path_valid("LR", &error));
	zassert_str_equal(error, "code must end with 0");
	zassert_false(route_path_valid("L0R0", &error));
	zassert_str_equal(error, "0 only allowed at the end");
	zassert_false(route_path_valid("lr0", &error));
	zassert_str_equal(error, "only L R S 0 allowed");
}

static void expect_save_error(const char *body, const char *expected)
{
	const char *error = NULL;

	zassert_not_equal(destinations_save_json(body, strlen(body), &error), 0, "%s", body);
	zassert_str_equal(error, expected, "%s", body);
}

ZTEST(parsers, test_tables_save_and_errors)
{
	const char *good = " { \"3\" : \"LR0\", \"1\":\"S0\" } ";
	const char *error = NULL;
	char json[DESTINATION_JSON_LEN];
	char code[ROUTE_CODE_MAX_LEN + 1];

	zassert_ok(destinations_save_json(good, strlen(good), &error));
	zassert_true(destinations_json(json, sizeof(json)) > 0);
	zassert_str_equal(json, "{\"1\":\"S0\",\"3\":\"LR0\"}");

	expect_save_error("", "EmptyInput");
	expect_save_error("{\"1\":\"S0\"", "IncompleteInput");
	expect_save_error("{\"1\" \"S0\"}", "InvalidInput");
	expect_save_error("{\"1\":\"S0\"} x", "InvalidInput");
	expect_save_error("[1]", "root must be object");
	expect_save_error("{\"a\":\"S0\"}", "id must be an integer from 1 to 2147483647");
	expect_save_error("{\"0\":\"S0\"}", "id must be an integer from 1 to 2147483647");
	expect_save_error("{\"2147483648\":\"S0\"}",
			  "id must be an integer from 1 to 2147483647");
	expect_save_error("{\"1\":\"S0\",\"01\":\"L0\"}", "duplicate destination id");
	expect_save_error("{\"1\":5}", "code must be string");
	expect_save_error("{\"1\":\"X0\"}", "only L R S 0 allowed");

	/* Lỗi nào cũng giữ bảng cũ */
	zassert_true(destinations_lookup(3, code));
	zassert_str_equal(code, "LR0");

	fake.fail_write = true;
	expect_save_error("{\"9\":\"S0\"}", "cannot write tables.json");
	zassert_false(destinations_lookup(9, code));
}

ZTEST(parsers, test_tables_limit)
{
	static char body[1024];
	const char *error = NULL;
	int n = 0;

	for (int i = 1; i <= DESTINATION_MAX + 1; i++) {
		n += snprintk(&body[n], sizeof(body) - n, "%c\"%d\":\"S0\"",
			      i == 1 ? '{' : ',', i);
	}
	body[n++] = '}';

	zassert_not_equal(destinations_save_json(body, n, &error), 0);
	zassert_str_equal(error, "too many destinations (max 32)");
}

ZTEST(parsers, test_tables_load)
{
	char json[DESTINATION_JSON_LEN];
	const char *stored = "{\"2\":\"L0\",\"x\":\"S0\",\"4\":\"bad\",\"5\":7}";

	/* Chưa có file: ghi và nạp bảng mặc định */
	fake.file_len = -1;
	zassert_ok(destinations_init("{\"1\":\"S0\"}"));
	zassert_true(fake.file_len > 0);

	/* Nạp file: bỏ qua mục hỏng */
	zassert_ok(tables_storage_write(stored, strlen(stored)));
	zassert_ok(destinations_load());
	(void)destinations_json(json, sizeof(json));
	zassert_str_equal(json, "{\"2\":\"L0\"}");

	/* File hỏng: giữ bảng đang có */
	zassert_ok(tables_storage_write("{oops", 5));
	zassert_not_equal(destinations_load(), 0);
	(void)destinations_json(json, sizeof(json));
	zassert_str_equal(json, "{\"2\":\"L0\"}");
}

ZTEST(parsers, test_json_syntax)
{
	const char *nested = "{\"a\":{\"b\":[1,{\"c\":null}],\"d\":-1.5e3},\"e\":true}";
	const char *deep = "{\"a\":[[[[[[[[[[1]]]]]]]]]]}";
	const char *bad[] = {"{\"a\":01}", "{\"a\":1,}", "{,}", "{\"a\":[1,]}",
			     "{\"a\":\"\\x\"}", "{\"a\":tru}", "{a:1}"};

	zassert_equal(json_flat_parse(nested, strlen(nested), NULL, NULL), JSON_FLAT_OK);
	zassert_equal(json_flat_parse(deep, strlen(deep), NULL, NULL), JSON_FLAT_TOO_DEEP);

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_not_equal(json_flat_parse(bad[i], strlen(bad[i]), NULL, NULL),
				  JSON_FLAT_OK, "%s", bad[i]);
	}

	/* Cắt ở bất kỳ vị trí nào cũng không được coi là hợp lệ */
	for (size_t len = 0; len < strlen(nested); len++) {
		zassert_not_equal(json_flat_parse(nested, len, NULL, NULL), JSON_FLAT_OK,
				  "len %u", (unsigned int)len);
	}
}

ZTEST(parsers, test_json_strings)
{
	const char *raw = "A\\u00e9\\n\\\"";
	const struct json_flat_value value = {
		.type = JSON_FLAT_STRING, .raw = raw, .raw_len = strlen(raw)};
	char out[16];
	char tiny[3];

	zassert_true(json_flat_get_string(&value, out, sizeof(out)));
	zassert_str_equal(out, "A\xc3\xa9\n\"");
	zassert_false(json_flat_get_string(&value, tiny, sizeof(tiny)));

	json_flat_escape(out, sizeof(out), "a\"b\\c\n");
	zassert_str_equal(out, "a\\\"b\\\\c\\u000a");
}

/* Độ rộng xung (us) tương ứng một khoảng cách */
static uint32_t pulse_for(float cm)
{
	return (uint32_t)(cm * 2.0f / 0.0343f);
}

ZTEST(parsers, test_obstacle_hysteresis)
{
	struct obstacle_filter f = {0};

	obstacle_filter_echo(&f, pulse_for(12.0f));
	zassert_false(f.detected);
	obstacle_filter_echo(&f, pulse_for(8.0f));
	zassert_true(f.detected);
	obstacle_filter_echo(&f, pulse_for(12.0f));
	zassert_true(f.detected, "vẫn dưới ngưỡng xoá 15 cm");
	obstacle_filter_echo(&f, pulse_for(16.0f));
	zassert_false(f.detected);

	obstacle_filter_echo(&f, pulse_for(5.0f));
	obstacle_filter_no_echo(&f);
	zassert_true(f.detected);
	obstacle_filter_echo(&f, pulse_for(5.0f)); /* đếm lại từ đầu */
	obstacle_filter_no_echo(&f);
	zassert_true(f.detected);
	obstacle_filter_echo(&f, pulse_for(500.0f)); /* ngoài tầm = không echo */
	zassert_false(f.detected);

	/* Không echo không bao giờ tự bật trạng thái vật cản */
	obstacle_filter_no_echo(&f);
	obstacle_filter_no_echo(&f);
	zassert_false(f.detected);
}

ZTEST(parsers, test_wav)
{
	static const uint8_t file[] = {
#include "welcome.wav.inc"
	};
	struct wav_pcm pcm;
	uint8_t broken[64];

	zassert_ok(wav_parse(file, sizeof(file), &pcm));
	zassert_equal(pcm.sample_rate, 16000);
	zassert_true(pcm.bytes > 16000 && pcm.bytes < sizeof(file));

	zassert_not_equal(wav_parse(file, 20, &pcm), 0);
	/* Cắt giữa chunk data (file có chunk phụ nằm sau data) */
	zassert_not_equal(wav_parse(file, sizeof(file) / 2, &pcm), 0);
	memcpy(broken, file, sizeof(broken));
	broken[22] = 2; /* stereo */
	zassert_not_equal(wav_parse(broken, sizeof(broken), &pcm), 0);
}

ZTEST(parsers, test_clock_text)
{
	char text[CLOCK_TEXT_LEN];
	const int64_t t = 5 * 86400 + 13 * 3600 + 7 * 60;

	clock_format(false, t, text);
	zassert_str_equal(text, "----");
	clock_format(true, t, text);
	zassert_str_equal(text, "13.07");
	clock_format(true, t + 1, text);
	zassert_str_equal(text, "1307");
}
