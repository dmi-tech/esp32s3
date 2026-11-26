#pragma once

/*
 * Parser tối giản cho một object JSON, đủ cho lệnh WebSocket và bảng tuyến
 * (Zephyr JSON library không duyệt được key tuỳ ý). Giá trị lồng nhau chỉ được
 * kiểm tra cú pháp rồi bỏ qua.
 */

#include <stdbool.h>
#include <stddef.h>

#define JSON_FLAT_MAX_DEPTH 8
#define JSON_FLAT_KEY_LEN 32

enum json_flat_type {
	JSON_FLAT_STRING,
	JSON_FLAT_INT,  /* số không có phần thập phân hay số mũ */
	JSON_FLAT_REAL,
	JSON_FLAT_BOOL,
	JSON_FLAT_NULL,
	JSON_FLAT_NESTED,
};

struct json_flat_value {
	enum json_flat_type type;
	const char *raw; /* chuỗi: nội dung giữa hai dấu nháy, còn escape */
	size_t raw_len;
};

enum json_flat_status {
	JSON_FLAT_OK = 0,
	JSON_FLAT_EMPTY = -1,
	JSON_FLAT_INCOMPLETE = -2,
	JSON_FLAT_INVALID = -3,
	JSON_FLAT_NOT_OBJECT = -4,
	JSON_FLAT_TOO_DEEP = -5,
};

/* key dài quá JSON_FLAT_KEY_LEN-1 được truyền vào dưới dạng chuỗi rỗng */
typedef void (*json_flat_member_cb)(const char *key, const struct json_flat_value *value,
				    void *ctx);

/* Kiểm tra toàn bộ tài liệu trước, chỉ gọi cb khi tài liệu hợp lệ */
enum json_flat_status json_flat_parse(const char *text, size_t len, json_flat_member_cb cb,
				      void *ctx);

/* Tên lỗi giống ArduinoJson */
const char *json_flat_error(enum json_flat_status status);

/* false nếu không phải chuỗi hoặc không vừa bộ đệm */
bool json_flat_get_string(const struct json_flat_value *value, char *out, size_t out_len);

/* false nếu không phải số nguyên nằm trong miền int */
bool json_flat_get_int(const struct json_flat_value *value, int *out);

/* false nếu không phải số hữu hạn */
bool json_flat_get_float(const struct json_flat_value *value, float *out);

/* Escape để đặt vào giữa hai dấu nháy; cắt bớt cho vừa bộ đệm */
void json_flat_escape(char *out, size_t out_len, const char *in);
