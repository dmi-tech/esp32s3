#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/wifi_mgmt.h>

#include "audio.h"
#include "network.h"
#include "robot_config.h"
#include "robot_state.h"

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

#define NETWORK_PRIORITY K_PRIO_PREEMPT(10)
#define NETWORK_STACK_SIZE 4096

#define NETWORK_POLL_MS 500
#define WIFI_RETRY_MS 5000
#define SNTP_TIMEOUT_MS 3000U
#define SNTP_RETRY_MS 10000
#define SNTP_RESYNC_MS (3600 * 1000)

static K_THREAD_STACK_DEFINE(network_stack, NETWORK_STACK_SIZE);
static struct k_thread network_data;

static K_MUTEX_DEFINE(status_lock);
static char status_ip[NETWORK_IP_LEN] = "0.0.0.0";
static int status_rssi;
static bool time_valid;
static int64_t time_epoch_s;
static int64_t time_uptime_ms;

static struct net_mgmt_event_callback wifi_events;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	ARG_UNUSED(iface);

	const struct wifi_status *status = cb->info;

	if (event == NET_EVENT_WIFI_CONNECT_RESULT && status->status != 0) {
		/* 1 lỗi chung, 2 sai mật khẩu, 3 hết giờ, 4 không thấy AP */
		LOG_WRN("[WIFI] connect failed, status %d", status->status);
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("[WIFI] disconnected");
	}
}

static void wifi_connect(struct net_if *iface)
{
	static const char ssid[] = CONFIG_APP_WIFI_SSID;
	static const char psk[] = CONFIG_APP_WIFI_PSK;
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)ssid,
		.ssid_length = sizeof(ssid) - 1,
		.psk = (const uint8_t *)psk,
		.psk_length = sizeof(psk) - 1,
		.security = sizeof(psk) > 1 ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.mfp = WIFI_MFP_OPTIONAL,
	};

	const int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));

	if (ret == -EALREADY) {
		LOG_DBG("[WIFI] connection already in progress");
	} else if (ret != 0) {
		LOG_WRN("[WIFI] connect request failed: %d", ret);
	} else {
		LOG_INF("[WIFI] connecting to \"%s\"", ssid);
	}
}

/* Đọc IP và RSSI hiện tại; trả về true khi đã có địa chỉ IPv4 */
static bool refresh_status(struct net_if *iface)
{
	char ip[NETWORK_IP_LEN] = "0.0.0.0";
	int rssi = 0;
	const struct net_in_addr *addr =
		net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	const bool connected =
		addr != NULL &&
		net_addr_ntop(NET_AF_INET, addr, ip, sizeof(ip)) != NULL;

	if (connected) {
		struct wifi_iface_status wifi = {0};

		if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &wifi, sizeof(wifi)) == 0) {
			rssi = wifi.rssi;
		}
	}

	k_mutex_lock(&status_lock, K_FOREVER);
	robot_copy_text(status_ip, sizeof(status_ip), connected ? ip : "0.0.0.0");
	status_rssi = rssi;
	k_mutex_unlock(&status_lock);

	return connected;
}

static bool sync_time(void)
{
	static const char *const servers[] = {"pool.ntp.org", "time.google.com"};

	for (size_t i = 0; i < ARRAY_SIZE(servers); i++) {
		struct sntp_time now;
		const int ret = sntp_simple(servers[i], SNTP_TIMEOUT_MS, &now);

		if (ret == 0) {
			k_mutex_lock(&status_lock, K_FOREVER);
			time_epoch_s = (int64_t)now.seconds;
			time_uptime_ms = k_uptime_get();
			time_valid = true;
			k_mutex_unlock(&status_lock);
			LOG_INF("[NTP] synced with %s", servers[i]);
			return true;
		}

		LOG_WRN("[NTP] %s failed: %d", servers[i], ret);
	}

	return false;
}

static void network_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct net_if *iface = net_if_get_wifi_sta();

	if (iface == NULL) {
		LOG_ERR("[WIFI] no station interface");
		return;
	}

	if (sizeof(CONFIG_APP_WIFI_SSID) <= 1) {
		LOG_ERR("[WIFI] SSID chưa cấu hình; build với wifi.conf (xem README)");
		return;
	}

	bool was_connected = false;
	bool attempted = false;
	int64_t last_attempt = 0;
	int64_t next_sync = 0;

	while (true) {
		const bool connected = refresh_status(iface);
		const int64_t now = k_uptime_get();

		if (connected && !was_connected) {
			char ip[NETWORK_IP_LEN];
			int rssi;

			network_status(ip, &rssi);
			LOG_INF("[WIFI] CONNECTED, RSSI %d dBm", rssi);
			LOG_INF("[HTTP] http://%s/  hoặc  http://%s.local/", ip,
				CONFIG_NET_HOSTNAME);
			audio_request("welcome.mp3");
			next_sync = now;
		}
		was_connected = connected;

		if (!connected && (!attempted || now - last_attempt >= WIFI_RETRY_MS)) {
			attempted = true;
			last_attempt = now;
			wifi_connect(iface);
		}

		if (connected && now >= next_sync) {
			next_sync = k_uptime_get() +
				    (sync_time() ? SNTP_RESYNC_MS : SNTP_RETRY_MS);
		}

		k_msleep(NETWORK_POLL_MS);
	}
}

void network_start(void)
{
	net_mgmt_init_event_callback(&wifi_events, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_events);

	k_tid_t tid = k_thread_create(&network_data, network_stack,
				      K_THREAD_STACK_SIZEOF(network_stack), network_loop, NULL,
				      NULL, NULL, NETWORK_PRIORITY, 0, K_NO_WAIT);

	(void)k_thread_name_set(tid, "network");
}

void network_status(char ip[NETWORK_IP_LEN], int *rssi)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	robot_copy_text(ip, NETWORK_IP_LEN, status_ip);
	*rssi = status_rssi;
	k_mutex_unlock(&status_lock);
}

bool network_local_time(int64_t *seconds)
{
	k_mutex_lock(&status_lock, K_FOREVER);

	const bool valid = time_valid;

	if (valid) {
		*seconds = time_epoch_s + (k_uptime_get() - time_uptime_ms) / 1000 +
			   NTP_TIMEZONE_OFFSET_S;
	}

	k_mutex_unlock(&status_lock);

	return valid;
}
