#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/websocket.h>
#include <zephyr/sys/fdtable.h>

/*
 * Header riêng của Zephyr: chỉ dùng websocket_context_foreach() để biết socket
 * TCP bên dưới, cần cho hạn chờ gửi (SO_SNDTIMEO) và drop_client().
 * Zephyr được pin ở west.yml.
 */
#include "websocket_internal.h"

#include "destination_manager.h"
#include "events.h"
#include "json_flat.h"
#include "network.h"
#include "web_protocol.h"
#include "web_server.h"

LOG_MODULE_REGISTER(web, LOG_LEVEL_INF);

#define WEB_PRIORITY K_PRIO_PREEMPT(9)
#define WEB_STACK_SIZE 6144

#define WS_CLIENT_MAX 4
#define WS_RX_BUFFER_LEN 512
#define WS_MESSAGE_LEN 384
#define WS_SEND_TIMEOUT_MS 200
#define WS_RECV_BURST 16
#define WEB_IDLE_POLL_MS 20
#define TABLES_BODY_LEN 8192

BUILD_ASSERT(WS_CLIENT_MAX == CONFIG_WEBSOCKET_MAX_CONTEXTS);

/* ---------------- File tĩnh (gzip, nhúng trong firmware) ---------------- */

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};
static const uint8_t app_js_gz[] = {
#include "app.js.gz.inc"
};
static const uint8_t style_css_gz[] = {
#include "style.css.gz.inc"
};

#define STATIC_RESOURCE(name, data, mime)                                                 \
	static struct http_resource_detail_static name##_detail = {                       \
		.common = {                                                               \
			.type = HTTP_RESOURCE_TYPE_STATIC,                                \
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),               \
			.content_encoding = "gzip",                                       \
			.content_type = mime,                                             \
		},                                                                        \
		.static_data = data,                                                      \
		.static_data_len = sizeof(data),                                          \
	}

STATIC_RESOURCE(index_page, index_html_gz, "text/html; charset=utf-8");
STATIC_RESOURCE(app_script, app_js_gz, "text/javascript; charset=utf-8");
STATIC_RESOURCE(style_sheet, style_css_gz, "text/css; charset=utf-8");

/* ---------------- REST /api/tables ---------------- */

static const struct http_header json_headers[] = {
	{.name = "Content-Type", .value = "application/json"},
	{.name = "Cache-Control", .value = "no-store"},
};

static void respond_json(struct http_response_ctx *rsp, enum http_status code,
			 const char *body, size_t len)
{
	rsp->status = code;
	rsp->headers = json_headers;
	rsp->header_count = ARRAY_SIZE(json_headers);
	rsp->body = (const uint8_t *)body;
	rsp->body_len = len;
	rsp->final_chunk = true;
}

/* Server chỉ cho một client dùng resource này tại một thời điểm */
static int tables_handler(struct http_client_ctx *client, enum http_transaction_status status,
			  const struct http_request_ctx *req, struct http_response_ctx *rsp,
			  void *user_data)
{
	static char body[TABLES_BODY_LEN];
	static size_t body_len;
	static bool overflow;
	static char response[DESTINATION_JSON_LEN];

	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		body_len = 0;
		overflow = false;
		return 0;
	}

	if (client->method == HTTP_PUT && req->data_len > 0U) {
		if (req->data_len < sizeof(body) - body_len) {
			memcpy(&body[body_len], req->data, req->data_len);
			body_len += req->data_len;
		} else {
			overflow = true;
		}
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	if (client->method == HTTP_GET) {
		const int len = destinations_json(response, sizeof(response));

		if (len < 0) {
			return len;
		}
		respond_json(rsp, HTTP_200_OK, response, (size_t)len);
		return 0;
	}

	const char *error = "IncompleteInput"; /* body quá dài */
	const bool saved =
		!overflow && destinations_save_json(body, body_len, &error) == 0;

	body_len = 0;
	overflow = false;

	if (saved) {
		static const char ok[] = "{\"ok\":true}";

		respond_json(rsp, HTTP_200_OK, ok, sizeof(ok) - 1);
	} else {
		char escaped[96];

		json_flat_escape(escaped, sizeof(escaped), error);

		const int len = snprintk(response, sizeof(response),
					 "{\"ok\":false,\"error\":\"%s\"}", escaped);

		respond_json(rsp, HTTP_400_BAD_REQUEST, response, (size_t)len);
	}

	return 0;
}

static struct http_resource_detail_dynamic tables_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_PUT),
	},
	.cb = tables_handler,
};

/* ---------------- WebSocket /ws ---------------- */

struct ws_client {
	int sock;      /* fd WebSocket, -1 = trống */
	int real_sock; /* fd TCP bên dưới */
	uint8_t *rx_buffer;
	bool greet;
	bool discard;
	size_t message_len;
	char message[WS_MESSAGE_LEN];
};

/*
 * Thư viện websocket giữ dữ liệu đọc dở trong bộ đệm đăng ký cùng socket, nên
 * mỗi client cần bộ đệm riêng. Dư một bộ đệm để luôn có sẵn cái chưa dùng cho
 * kết nối kế tiếp.
 */
static uint8_t ws_rx_buffers[WS_CLIENT_MAX + 1][WS_RX_BUFFER_LEN];
static struct ws_client clients[WS_CLIENT_MAX];

/* Luồng HTTP chỉ thêm client vào ô trống; chỉ luồng web xoá client */
static K_MUTEX_DEFINE(clients_lock);

static int ws_accept(int ws_socket, struct http_request_ctx *request_ctx, void *user_data);

static struct http_resource_detail_websocket ws_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = ws_accept,
	.data_buffer = ws_rx_buffers[0],
	.data_buffer_len = WS_RX_BUFFER_LEN,
};

struct sock_lookup {
	int ws_sock;
	int real_sock;
};

static void match_context(struct websocket_context *ctx, void *user_data)
{
	struct sock_lookup *lookup = user_data;

	if (ctx->sock == lookup->ws_sock) {
		lookup->real_sock = ctx->real_sock;
	}
}

/* fd TCP của một websocket đang sống, hoặc -1 */
static int websocket_real_sock(int ws_sock)
{
	struct sock_lookup lookup = {.ws_sock = ws_sock, .real_sock = -1};

	websocket_context_foreach(match_context, &lookup);

	return lookup.real_sock;
}

static bool buffer_in_use(const uint8_t *buffer)
{
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		if (clients[i].sock >= 0 && clients[i].rx_buffer == buffer) {
			return true;
		}
	}
	return false;
}

static int ws_accept(int ws_socket, struct http_request_ctx *request_ctx, void *user_data)
{
	int ret = -ENOENT;

	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	const int real_sock = websocket_real_sock(ws_socket);

	if (real_sock < 0) {
		return ret;
	}

	k_mutex_lock(&clients_lock, K_FOREVER);

	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		struct ws_client *c = &clients[i];

		if (c->sock >= 0) {
			continue;
		}

		const struct zsock_timeval timeout = {
			.tv_usec = WS_SEND_TIMEOUT_MS * 1000,
		};

		(void)zsock_setsockopt(real_sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDTIMEO,
				       &timeout, sizeof(timeout));

		c->real_sock = real_sock;
		c->rx_buffer = ws_detail.data_buffer;
		c->greet = true;
		c->discard = false;
		c->message_len = 0;
		c->sock = ws_socket; /* ghi sau cùng: ô bắt đầu có hiệu lực */

		for (int b = 0; b <= WS_CLIENT_MAX; b++) {
			if (!buffer_in_use(ws_rx_buffers[b])) {
				ws_detail.data_buffer = ws_rx_buffers[b];
				break;
			}
		}

		LOG_INF("[WS] Client #%d connected", i);
		ret = 0;
		break;
	}

	k_mutex_unlock(&clients_lock);

	if (ret != 0) {
		LOG_WRN("[WS] too many clients");
	}

	return ret;
}

static void forget_client(struct ws_client *c)
{
	LOG_INF("[WS] Client #%d disconnected", (int)(c - clients));

	k_mutex_lock(&clients_lock, K_FOREVER);
	c->sock = -1;
	k_mutex_unlock(&clients_lock);
}

/*
 * Thư viện tự huỷ context khi nhận frame CLOSE thiếu bit FIN (sai giao thức)
 * nhưng không đóng fd websocket lẫn fd TCP, và websocket_unregister() không còn
 * tìm thấy chúng. Giải phóng ở đây; không gọi close() trên fd websocket vì sẽ
 * giảm bộ đếm tham chiếu lần thứ hai. Frame CLOSE hợp lệ do finish_frame() xử lý.
 */
static void release_closed_by_peer(struct ws_client *c)
{
	zvfs_free_fd(c->sock);
	(void)zsock_close(c->real_sock);
	forget_client(c);
}

/*
 * Chủ động bỏ một client lỗi. Đóng TCP trước để frame CLOSE mà thư viện gửi
 * (không có hạn chờ) thất bại ngay thay vì treo luồng web.
 */
static void drop_client(struct ws_client *c)
{
	k_sched_lock();
	(void)zsock_close(c->real_sock);
	(void)zsock_close(c->sock);
	k_sched_unlock();
	forget_client(c);
}

static bool send_text(struct ws_client *c, const char *text, int len)
{
	if (c->sock < 0 || len < 0) {
		return false;
	}

	const int ret = websocket_send_msg(c->sock, (const uint8_t *)text, (size_t)len,
					   WEBSOCKET_OPCODE_DATA_TEXT, false, true,
					   WS_SEND_TIMEOUT_MS);

	if (ret < 0) {
		LOG_WRN("[WS] send failed: %d", ret);
		drop_client(c);
		return false;
	}

	return true;
}

static void broadcast(const char *text, int len)
{
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		(void)send_text(&clients[i], text, len);
	}
}

static int client_count(void)
{
	int count = 0;

	k_mutex_lock(&clients_lock, K_FOREVER);
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		count += clients[i].sock >= 0 ? 1 : 0;
	}
	k_mutex_unlock(&clients_lock);

	return count;
}

/* Bộ đệm gửi, chỉ luồng web dùng */
static char tx_buffer[768];

static int build_config(void)
{
	static struct robot_state snapshot;

	robot_state_snapshot(&snapshot);
	return web_config_json(tx_buffer, sizeof(tx_buffer), &snapshot);
}

static int build_telemetry(void)
{
	static struct robot_state snapshot;
	char ip[NETWORK_IP_LEN];
	int rssi;

	robot_state_snapshot(&snapshot);
	network_status(ip, &rssi);

	return web_telemetry_json(tx_buffer, sizeof(tx_buffer), &snapshot, ip, rssi);
}

static void handle_message(struct ws_client *c, size_t message_len)
{
	struct web_reply reply;

	web_handle_command(c->message, message_len, &reply);

	if (reply.send_goto_result) {
		const int len = web_goto_result_json(tx_buffer, sizeof(tx_buffer), reply.ok,
						     reply.message);

		(void)send_text(c, tx_buffer, len);
	}
}

static void append_payload(struct ws_client *c, const uint8_t *data, size_t len)
{
	if (len > sizeof(c->message) - c->message_len) {
		c->discard = true;
	} else {
		memcpy(&c->message[c->message_len], data, len);
		c->message_len += len;
	}
}

/* Một frame đã nhận đủ */
static void finish_frame(struct ws_client *c, uint32_t type)
{
	/* Như bản cũ: chỉ nhận message text trong một frame duy nhất */
	const uint32_t text = WEBSOCKET_FLAG_TEXT | WEBSOCKET_FLAG_FINAL;
	const size_t len = c->message_len;
	const bool too_long = c->discard;

	c->message_len = 0;
	c->discard = false;

	if ((type & WEBSOCKET_FLAG_CLOSE) != 0U) {
		/* Thư viện không tự trả lời: gửi CLOSE (kèm mã) rồi đóng */
		(void)websocket_send_msg(c->sock, (const uint8_t *)c->message,
					 too_long ? 0U : len, WEBSOCKET_OPCODE_CLOSE, false,
					 true, WS_SEND_TIMEOUT_MS);
		drop_client(c);
	} else if (too_long) {
		LOG_WRN("[WS] message too long; ignored");
	} else if ((type & WEBSOCKET_FLAG_PING) != 0U) {
		(void)websocket_send_msg(c->sock, (const uint8_t *)c->message, len,
					 WEBSOCKET_OPCODE_PONG, false, true,
					 WS_SEND_TIMEOUT_MS);
	} else if ((type & text) == text) {
		handle_message(c, len);
	}
}

/* Đọc hết dữ liệu đang chờ của một client, không chặn */
static void receive_from(struct ws_client *c)
{
	const int sock = c->sock;

	/* Dừng nếu client bị bỏ (ô có thể được dùng lại cho kết nối mới) */
	for (int n = 0; n < WS_RECV_BURST && c->sock == sock; n++) {
		static uint8_t chunk[WS_RX_BUFFER_LEN];
		uint32_t type = 0;
		uint64_t remaining = 0;
		const int ret = websocket_recv_msg(c->sock, chunk, sizeof(chunk), &type,
						   &remaining, 0);

		if (websocket_real_sock(c->sock) < 0) {
			release_closed_by_peer(c);
			return;
		}

		if (ret == -EAGAIN) {
			return;
		}

		if (ret < 0) {
			drop_client(c);
			return;
		}

		if (ret > 0) {
			append_payload(c, chunk, (size_t)ret);
		}

		/*
		 * Khi frame mới đến một phần, thư viện trả dữ liệu nhưng không điền
		 * type và remaining: gom lại, chờ tới khi type được điền.
		 */
		if (type == 0U) {
			if (ret == 0) {
				return;
			}
			continue;
		}

		if (remaining != 0U) {
			continue;
		}

		finish_frame(c, type);
	}
}

static void service_clients(void)
{
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		struct ws_client *c = &clients[i];

		k_mutex_lock(&clients_lock, K_FOREVER);
		const bool live = c->sock >= 0;
		const bool greet = live && c->greet;

		c->greet = false;
		k_mutex_unlock(&clients_lock);

		if (!live) {
			continue;
		}

		if (greet && send_text(c, tx_buffer, build_config())) {
			(void)send_text(c, tx_buffer, build_telemetry());
		}

		receive_from(c);
	}
}

static void publish(int64_t *next_telemetry_ms)
{
	char message[EVENT_MESSAGE_LEN];

	/* Luôn lấy hết sự kiện, kể cả khi không có client */
	while (event_take(message)) {
		if (client_count() > 0) {
			broadcast(tx_buffer,
				  web_event_json(tx_buffer, sizeof(tx_buffer), message));
		}
	}

	if (event_take_config_request() && client_count() > 0) {
		broadcast(tx_buffer, build_config());
	}

	const int64_t now = k_uptime_get();

	if (now >= *next_telemetry_ms) {
		*next_telemetry_ms = now + TELEMETRY_INTERVAL;
		if (client_count() > 0) {
			broadcast(tx_buffer, build_telemetry());
		}
	}
}

/* Ngủ đến khi có dữ liệu mới hoặc hết WEB_IDLE_POLL_MS */
static void wait_for_input(void)
{
	struct zsock_pollfd fds[WS_CLIENT_MAX];
	int count = 0;

	k_mutex_lock(&clients_lock, K_FOREVER);
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		if (clients[i].sock >= 0) {
			fds[count].fd = clients[i].sock;
			fds[count].events = ZSOCK_POLLIN;
			fds[count].revents = 0;
			count++;
		}
	}
	k_mutex_unlock(&clients_lock);

	if (count == 0) {
		k_msleep(WEB_IDLE_POLL_MS);
	} else if (zsock_poll(fds, count, WEB_IDLE_POLL_MS) < 0) {
		LOG_WRN("[WS] poll failed: %d", errno);
		k_msleep(WEB_IDLE_POLL_MS);
	}
}

static void web_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t next_telemetry_ms = 0;

	while (true) {
		service_clients();
		publish(&next_telemetry_ms);
		wait_for_input();
	}
}

/* ---------------- Service ---------------- */

#ifndef APP_HTTP_PORT
#define APP_HTTP_PORT 80
#endif

static uint16_t http_port = APP_HTTP_PORT;

HTTP_SERVICE_DEFINE(robot_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL,
		    NULL, NULL);

HTTP_RESOURCE_DEFINE(root_resource, robot_http, "/", &index_page_detail);
HTTP_RESOURCE_DEFINE(index_resource, robot_http, "/index.html", &index_page_detail);
HTTP_RESOURCE_DEFINE(script_resource, robot_http, "/app.js", &app_script_detail);
HTTP_RESOURCE_DEFINE(style_resource, robot_http, "/style.css", &style_sheet_detail);
HTTP_RESOURCE_DEFINE(tables_resource, robot_http, "/api/tables", &tables_detail);
HTTP_RESOURCE_DEFINE(ws_resource, robot_http, "/ws", &ws_detail);

static K_THREAD_STACK_DEFINE(web_stack, WEB_STACK_SIZE);
static struct k_thread web_data;

int web_server_start(void)
{
	for (int i = 0; i < WS_CLIENT_MAX; i++) {
		clients[i].sock = -1;
	}

	const int ret = http_server_start();

	if (ret != 0) {
		LOG_ERR("[HTTP] start failed: %d", ret);
		return ret;
	}

	k_tid_t tid = k_thread_create(&web_data, web_stack, K_THREAD_STACK_SIZEOF(web_stack),
				      web_loop, NULL, NULL, NULL, WEB_PRIORITY, K_FP_REGS,
				      K_NO_WAIT);

	(void)k_thread_name_set(tid, "web");
	LOG_INF("[HTTP] Server started on port 80");

	return 0;
}
