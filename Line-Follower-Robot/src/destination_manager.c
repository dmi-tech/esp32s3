#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "destination_manager.h"
#include "json_flat.h"
#include "robot_state.h"

LOG_MODULE_REGISTER(tables, LOG_LEVEL_INF);

struct destination {
	int id;
	char code[ROUTE_CODE_MAX_LEN + 1];
};

struct table {
	size_t count;
	struct destination entry[DESTINATION_MAX]; /* sắp theo id tăng dần */
};

/* Mọi bộ đệm tĩnh bên dưới được bảo vệ bởi table_mutex */
static K_MUTEX_DEFINE(table_mutex);
static struct table active;
static struct table pending;
static char io_buf[DESTINATION_JSON_LEN];

static struct destination *table_find(struct table *t, int id)
{
	for (size_t i = 0; i < t->count; i++) {
		if (t->entry[i].id == id) {
			return &t->entry[i];
		}
	}
	return NULL;
}

/* Người gọi đảm bảo id chưa có và bảng còn chỗ */
static void table_insert(struct table *t, int id, const char *code)
{
	size_t pos = 0;

	while (pos < t->count && t->entry[pos].id < id) {
		pos++;
	}
	memmove(&t->entry[pos + 1], &t->entry[pos], (t->count - pos) * sizeof(t->entry[0]));
	t->entry[pos].id = id;
	robot_copy_text(t->entry[pos].code, sizeof(t->entry[pos].code), code);
	t->count++;
}

struct parse_ctx {
	bool strict; /* lưu từ web: lỗi nào cũng từ chối; nạp file: bỏ qua mục lỗi */
	const char *error;
};

static void parse_member(const char *key, const struct json_flat_value *value, void *arg)
{
	struct parse_ctx *ctx = arg;
	char code[128];
	char *end = NULL;
	const char *error = NULL;

	if (ctx->error != NULL) {
		return;
	}

	errno = 0;
	const long id = strtol(key, &end, 10);

	if (end == key || *end != '\0' || id <= 0 || errno != 0 || id > INT_MAX) {
		error = "id must be an integer from 1 to 2147483647";
	} else if (table_find(&pending, (int)id) != NULL) {
		error = "duplicate destination id";
	} else if (value->type != JSON_FLAT_STRING) {
		error = "code must be string";
	} else if (!json_flat_get_string(value, code, sizeof(code))) {
		error = "code too long";
	} else if (!route_path_valid(code, &error)) {
		/* error đã được gán */
	} else if (pending.count >= DESTINATION_MAX) {
		error = "too many destinations (max 32)";
	} else {
		table_insert(&pending, (int)id, code);
		return;
	}

	if (ctx->strict) {
		ctx->error = error;
	} else {
		LOG_WRN("[TABLES] skip %s: %s", key, error);
	}
}

/* Phân tích vào "pending". Trả về NULL nếu thành công. */
static const char *parse_into_pending(const char *text, size_t len, bool strict)
{
	struct parse_ctx ctx = {.strict = strict};

	pending.count = 0;

	const enum json_flat_status status = json_flat_parse(text, len, parse_member, &ctx);

	if (status != JSON_FLAT_OK) {
		return json_flat_error(status);
	}

	return ctx.error;
}

/* Code chỉ gồm L R S 0 nên không cần escape */
static int serialize(const struct table *t, char *out, size_t out_len)
{
	size_t n = 0;

	if (out_len < 3U) {
		return -ENOSPC;
	}
	out[n++] = '{';

	for (size_t i = 0; i < t->count; i++) {
		const int written =
			snprintk(&out[n], out_len - n, "%s\"%d\":\"%s\"", i == 0U ? "" : ",",
				 t->entry[i].id, t->entry[i].code);

		if (written < 0 || (size_t)written >= out_len - n) {
			return -ENOSPC;
		}
		n += (size_t)written;
	}

	if (n + 2U > out_len) {
		return -ENOSPC;
	}
	out[n++] = '}';
	out[n] = '\0';

	return (int)n;
}

static int load_locked(void)
{
	const int len = tables_storage_read(io_buf, sizeof(io_buf));

	if (len < 0) {
		LOG_WRN("[TABLES] read failed: %d", len);
		return len;
	}

	const char *error = parse_into_pending(io_buf, (size_t)len, false);

	if (error != NULL) {
		LOG_WRN("[TABLES] parse error: %s", error);
		return -EINVAL;
	}

	active = pending;
	LOG_INF("[TABLES] loaded %u destination(s)", (unsigned int)active.count);

	return 0;
}

int destinations_load(void)
{
	k_mutex_lock(&table_mutex, K_FOREVER);
	const int ret = load_locked();

	k_mutex_unlock(&table_mutex);
	return ret;
}

int destinations_init(const char *default_json)
{
	k_mutex_lock(&table_mutex, K_FOREVER);
	active.count = 0;

	int ret = load_locked();

	if (ret == -ENOENT && default_json != NULL) {
		ret = tables_storage_write(default_json, strlen(default_json));
		if (ret == 0) {
			ret = load_locked();
		}
	}

	k_mutex_unlock(&table_mutex);
	return ret;
}

int destinations_save_json(const char *body, size_t len, const char **error)
{
	int ret = -EINVAL;

	k_mutex_lock(&table_mutex, K_FOREVER);

	*error = parse_into_pending(body, len, true);
	if (*error == NULL) {
		const int out_len = serialize(&pending, io_buf, sizeof(io_buf));

		if (out_len < 0 || tables_storage_write(io_buf, (size_t)out_len) != 0) {
			*error = "cannot write tables.json";
			ret = -EIO;
		} else {
			active = pending;
			ret = 0;
			LOG_INF("[TABLES] saved %u destination(s)",
				(unsigned int)active.count);
		}
	}

	k_mutex_unlock(&table_mutex);
	return ret;
}

int destinations_json(char *out, size_t out_len)
{
	k_mutex_lock(&table_mutex, K_FOREVER);
	const int ret = serialize(&active, out, out_len);

	k_mutex_unlock(&table_mutex);
	return ret;
}

bool destinations_lookup(int id, char code[ROUTE_CODE_MAX_LEN + 1])
{
	bool found = false;

	k_mutex_lock(&table_mutex, K_FOREVER);

	const struct destination *d = table_find(&active, id);

	if (d != NULL) {
		robot_copy_text(code, ROUTE_CODE_MAX_LEN + 1, d->code);
		found = true;
	}

	k_mutex_unlock(&table_mutex);
	return found;
}
