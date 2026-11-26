#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "motor_control.h"

#ifndef CONFIG_APP_BRINGUP_SHELL
#include "audio.h"
#include "clock_display.h"
#include "control_thread.h"
#include "destination_manager.h"
#include "network.h"
#include "obstacle_monitor.h"
#include "tables_storage.h"
#include "web_server.h"
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#ifndef CONFIG_APP_BRINGUP_SHELL
static const char default_tables[] = {
#include "tables.json.inc"
	0x00};

static void start_robot(void)
{
	/* Lỗi bộ nhớ hay âm thanh không ngăn robot chạy: bảng rỗng, không có tiếng */
	if (tables_storage_mount() == 0) {
		(void)destinations_init(default_tables);
	}

	if (audio_init() != 0) {
		LOG_ERR("audio unavailable");
	}

	if (clock_display_start() != 0) {
		LOG_ERR("clock display unavailable");
	}

	/* Không đo được vật cản thì không cho robot chạy */
	int ret = obstacle_monitor_init();

	if (ret != 0) {
		LOG_ERR("obstacle monitor init failed: %d; robot disabled", ret);
		return;
	}

	ret = control_thread_start();
	if (ret != 0) {
		LOG_ERR("control thread start failed: %d; robot disabled", ret);
		return;
	}

	network_start();
	(void)web_server_start();
}
#endif

int main(void)
{
	/* Motor phải dừng trước mọi khởi tạo khác */
	int ret = motor_control_init();

	if (ret != 0) {
		LOG_ERR("motor init failed: %d", ret);
		return ret;
	}

	stop_motors();

	ret = line_sensors_init();
	if (ret != 0) {
		LOG_ERR("line sensor init failed: %d", ret);
		return ret;
	}

	LOG_INF("ESP32-S3 LINE FOLLOWER (Zephyr)");

#ifdef CONFIG_APP_BRINGUP_SHELL
	LOG_WRN("BRING-UP build: luồng điều khiển và mạng không chạy");
#else
	start_robot();
#endif

	return 0;
}
