#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "json_flat.h"

struct cursor {
	const char *p;
	const char *end;
};

static void skip_space(struct cursor *c)
{
	while (c->p < c->end &&
	       (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n')) {
		c->p++;
	}
}

static int hex_digit(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return ch - 'a' + 10;
	}
	if (ch >= 'A' && ch <= 'F') {
		return ch - 'A' + 10;
	}
	return -1;
}

/* c->p trỏ vào dấu nháy mở; trả về nội dung thô */
static enum json_flat_status scan_string(struct cursor *c, struct json_flat_value *v)
{
	const char *start = ++c->p;

	while (c->p < c->end) {
		const unsigned char ch = (unsigned char)*c->p;

		if (ch == '"') {
			v->type = JSON_FLAT_STRING;
			v->raw = start;
			v->raw_len = (size_t)(c->p - start);
			c->p++;
			return JSON_FLAT_OK;
		}
		if (ch < 0x20U) {
			return JSON_FLAT_INVALID;
		}
		if (ch == '\\') {
			if (++c->p >= c->end) {
				return JSON_FLAT_INCOMPLETE;
			}
			if (*c->p == 'u') {
				for (int i = 0; i < 4; i++) {
					if (++c->p >= c->end) {
						return JSON_FLAT_INCOMPLETE;
					}
					if (hex_digit(*c->p) < 0) {
						return JSON_FLAT_INVALID;
					}
				}
			} else if (*c->p == '\0' || strchr("\"\\/bfnrt", *c->p) == NULL) {
				return JSON_FLAT_INVALID;
			}
		}
		c->p++;
	}

	return JSON_FLAT_INCOMPLETE;
}

static size_t scan_digits(struct cursor *c)
{
	const char *start = c->p;

	while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
		c->p++;
	}

	return (size_t)(c->p - start);
}

static enum json_flat_status need_digits(struct cursor *c)
{
	if (scan_digits(c) > 0U) {
		return JSON_FLAT_OK;
	}

	return c->p >= c->end ? JSON_FLAT_INCOMPLETE : JSON_FLAT_INVALID;
}

static enum json_flat_status scan_number(struct cursor *c, struct json_flat_value *v)
{
	const char *start = c->p;
	enum json_flat_status status;

	v->type = JSON_FLAT_INT;

	if (*c->p == '-') {
		c->p++;
	}

	if (c->p < c->end && *c->p == '0') {
		c->p++;
	} else {
		status = need_digits(c);
		if (status != JSON_FLAT_OK) {
			return status;
		}
	}

	if (c->p < c->end && *c->p == '.') {
		c->p++;
		v->type = JSON_FLAT_REAL;
		status = need_digits(c);
		if (status != JSON_FLAT_OK) {
			return status;
		}
	}

	if (c->p < c->end && (*c->p == 'e' || *c->p == 'E')) {
		c->p++;
		v->type = JSON_FLAT_REAL;
		if (c->p < c->end && (*c->p == '+' || *c->p == '-')) {
			c->p++;
		}
		status = need_digits(c);
		if (status != JSON_FLAT_OK) {
			return status;
		}
	}

	v->raw = start;
	v->raw_len = (size_t)(c->p - start);

	return JSON_FLAT_OK;
}

static enum json_flat_status scan_literal(struct cursor *c, struct json_flat_value *v,
					  const char *word, enum json_flat_type type)
{
	const size_t len = strlen(word);
	const size_t left = (size_t)(c->end - c->p);

	if (left < len) {
		return memcmp(c->p, word, left) == 0 ? JSON_FLAT_INCOMPLETE : JSON_FLAT_INVALID;
	}
	if (memcmp(c->p, word, len) != 0) {
		return JSON_FLAT_INVALID;
	}

	v->type = type;
	v->raw = c->p;
	v->raw_len = len;
	c->p += len;

	return JSON_FLAT_OK;
}

static enum json_flat_status scan_scalar(struct cursor *c, struct json_flat_value *v)
{
	switch (*c->p) {
	case '"':
		return scan_string(c, v);
	case 't':
		return scan_literal(c, v, "true", JSON_FLAT_BOOL);
	case 'f':
		return scan_literal(c, v, "false", JSON_FLAT_BOOL);
	case 'n':
		return scan_literal(c, v, "null", JSON_FLAT_NULL);
	default:
		if (*c->p == '-' || (*c->p >= '0' && *c->p <= '9')) {
			return scan_number(c, v);
		}
		return JSON_FLAT_INVALID;
	}
}

#define NEED_MORE(c)                                                                      \
	do {                                                                              \
		skip_space(c);                                                            \
		if ((c)->p >= (c)->end) {                                                 \
			return JSON_FLAT_INCOMPLETE;                                      \
		}                                                                         \
	} while (0)

/* Kiểm tra cú pháp một object/array lồng nhau, không dùng đệ quy */
static enum json_flat_status skip_nested(struct cursor *c, struct json_flat_value *v)
{
	char closer[JSON_FLAT_MAX_DEPTH];
	const char *start = c->p;
	struct json_flat_value item;
	enum json_flat_status status;
	int depth = 0;
	bool opened = true; /* vừa mở container, chưa có phần tử */

	closer[depth++] = *c->p == '{' ? '}' : ']';
	c->p++;

	while (depth > 0) {
		NEED_MORE(c);

		if (*c->p == closer[depth - 1]) {
			c->p++;
			depth--;
			opened = false;
			continue;
		}

		if (!opened) {
			if (*c->p != ',') {
				return JSON_FLAT_INVALID;
			}
			c->p++;
			NEED_MORE(c);
		}
		opened = false;

		if (closer[depth - 1] == '}') {
			if (*c->p != '"') {
				return JSON_FLAT_INVALID;
			}
			status = scan_string(c, &item);
			if (status != JSON_FLAT_OK) {
				return status;
			}
			NEED_MORE(c);
			if (*c->p != ':') {
				return JSON_FLAT_INVALID;
			}
			c->p++;
			NEED_MORE(c);
		}

		if (*c->p == '{' || *c->p == '[') {
			if (depth >= JSON_FLAT_MAX_DEPTH) {
				return JSON_FLAT_TOO_DEEP;
			}
			closer[depth++] = *c->p == '{' ? '}' : ']';
			c->p++;
			opened = true;
		} else {
			status = scan_scalar(c, &item);
			if (status != JSON_FLAT_OK) {
				return status;
			}
		}
	}

	v->type = JSON_FLAT_NESTED;
	v->raw = start;
	v->raw_len = (size_t)(c->p - start);

	return JSON_FLAT_OK;
}

static enum json_flat_status walk(const char *text, size_t len, json_flat_member_cb cb,
				  void *ctx)
{
	struct cursor c = {.p = text, .end = text + len};
	enum json_flat_status status;
	bool first = true;

	skip_space(&c);
	if (c.p >= c.end) {
		return JSON_FLAT_EMPTY;
	}

	if (*c.p != '{') {
		struct json_flat_value root;

		status = *c.p == '[' ? skip_nested(&c, &root) : scan_scalar(&c, &root);
		if (status != JSON_FLAT_OK) {
			return status;
		}
		skip_space(&c);
		return c.p == c.end ? JSON_FLAT_NOT_OBJECT : JSON_FLAT_INVALID;
	}
	c.p++;

	while (true) {
		struct json_flat_value key_value;
		struct json_flat_value value;
		char key[JSON_FLAT_KEY_LEN];

		NEED_MORE(&c);
		if (*c.p == '}') {
			c.p++;
			break;
		}
		if (!first) {
			if (*c.p != ',') {
				return JSON_FLAT_INVALID;
			}
			c.p++;
			NEED_MORE(&c);
		}
		first = false;

		if (*c.p != '"') {
			return JSON_FLAT_INVALID;
		}
		status = scan_string(&c, &key_value);
		if (status != JSON_FLAT_OK) {
			return status;
		}
		NEED_MORE(&c);
		if (*c.p != ':') {
			return JSON_FLAT_INVALID;
		}
		c.p++;
		NEED_MORE(&c);

		status = (*c.p == '{' || *c.p == '[') ? skip_nested(&c, &value)
						      : scan_scalar(&c, &value);
		if (status != JSON_FLAT_OK) {
			return status;
		}

		if (cb != NULL) {
			if (!json_flat_get_string(&key_value, key, sizeof(key))) {
				key[0] = '\0';
			}
			cb(key, &value, ctx);
		}
	}

	skip_space(&c);

	return c.p == c.end ? JSON_FLAT_OK : JSON_FLAT_INVALID;
}

enum json_flat_status json_flat_parse(const char *text, size_t len, json_flat_member_cb cb,
				      void *ctx)
{
	const enum json_flat_status status = walk(text, len, NULL, NULL);

	if (status != JSON_FLAT_OK || cb == NULL) {
		return status;
	}

	return walk(text, len, cb, ctx);
}

const char *json_flat_error(enum json_flat_status status)
{
	switch (status) {
	case JSON_FLAT_OK:
		return "Ok";
	case JSON_FLAT_EMPTY:
		return "EmptyInput";
	case JSON_FLAT_INCOMPLETE:
		return "IncompleteInput";
	case JSON_FLAT_NOT_OBJECT:
		return "root must be object";
	case JSON_FLAT_TOO_DEEP:
		return "TooDeep";
	default:
		return "InvalidInput";
	}
}

static uint32_t read_hex4(const char *p)
{
	uint32_t value = 0;

	for (int i = 0; i < 4; i++) {
		value = (value << 4) | (uint32_t)hex_digit(p[i]);
	}

	return value;
}

static size_t utf8_encode(uint32_t cp, char out[3])
{
	if (cp < 0x80U) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800U) {
		out[0] = (char)(0xC0U | (cp >> 6));
		out[1] = (char)(0x80U | (cp & 0x3FU));
		return 2;
	}
	out[0] = (char)(0xE0U | (cp >> 12));
	out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
	out[2] = (char)(0x80U | (cp & 0x3FU));
	return 3;
}

bool json_flat_get_string(const struct json_flat_value *value, char *out, size_t out_len)
{
	size_t n = 0;

	if (value->type != JSON_FLAT_STRING || out_len == 0U) {
		return false;
	}

	/* raw đã được scan_string kiểm tra cú pháp escape */
	for (size_t i = 0; i < value->raw_len; i++) {
		char bytes[3] = {value->raw[i]};
		size_t count = 1;

		if (value->raw[i] == '\\') {
			const char esc = value->raw[++i];

			switch (esc) {
			case 'b':
				bytes[0] = '\b';
				break;
			case 'f':
				bytes[0] = '\f';
				break;
			case 'n':
				bytes[0] = '\n';
				break;
			case 'r':
				bytes[0] = '\r';
				break;
			case 't':
				bytes[0] = '\t';
				break;
			case 'u': {
				const uint32_t cp = read_hex4(&value->raw[i + 1]);

				/* \u0000 và cặp surrogate không được dùng ở đây */
				if (cp == 0U || (cp >= 0xD800U && cp <= 0xDFFFU)) {
					return false;
				}
				count = utf8_encode(cp, bytes);
				i += 4;
				break;
			}
			default:
				bytes[0] = esc;
				break;
			}
		}

		if (n + count >= out_len) {
			out[0] = '\0';
			return false;
		}
		memcpy(&out[n], bytes, count);
		n += count;
	}

	out[n] = '\0';

	return true;
}

static bool copy_number(const struct json_flat_value *value, char *buf, size_t size)
{
	if (value->raw_len >= size) {
		return false;
	}
	memcpy(buf, value->raw, value->raw_len);
	buf[value->raw_len] = '\0';
	return true;
}

bool json_flat_get_int(const struct json_flat_value *value, int *out)
{
	char buf[24];

	if (value->type != JSON_FLAT_INT || !copy_number(value, buf, sizeof(buf))) {
		return false;
	}

	errno = 0;
	const long long parsed = strtoll(buf, NULL, 10);

	if (errno != 0 || parsed < INT_MIN || parsed > INT_MAX) {
		return false;
	}

	*out = (int)parsed;
	return true;
}

bool json_flat_get_float(const struct json_flat_value *value, float *out)
{
	char buf[48];

	if ((value->type != JSON_FLAT_INT && value->type != JSON_FLAT_REAL) ||
	    !copy_number(value, buf, sizeof(buf))) {
		return false;
	}

	const float parsed = (float)strtod(buf, NULL);

	if (!isfinite(parsed)) {
		return false;
	}

	*out = parsed;
	return true;
}

void json_flat_escape(char *out, size_t out_len, const char *in)
{
	size_t n = 0;

	if (out_len == 0U) {
		return;
	}

	for (; *in != '\0'; in++) {
		const unsigned char ch = (unsigned char)*in;
		char piece[7];

		if (ch == '"' || ch == '\\') {
			(void)snprintk(piece, sizeof(piece), "\\%c", ch);
		} else if (ch < 0x20U) {
			(void)snprintk(piece, sizeof(piece), "\\u%04x", ch);
		} else {
			piece[0] = (char)ch;
			piece[1] = '\0';
		}

		const size_t len = strlen(piece);

		if (n + len >= out_len) {
			break;
		}
		memcpy(&out[n], piece, len);
		n += len;
	}

	out[n] = '\0';
}
