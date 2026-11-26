#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "motor_control.h"
#include "safety.h"

LOG_MODULE_REGISTER(safety, LOG_LEVEL_INF);

#define MONITOR_PERIOD_MS 20

static const struct device *const watchdog = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int watchdog_channel;
static atomic_t last_heartbeat_ms;
static atomic_t stalled;

/* Chạy trong ngắt nên vẫn hoạt động khi một luồng cooperative bị kẹt */
static void monitor_expired(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	const uint32_t age =
		k_uptime_get_32() - (uint32_t)atomic_get(&last_heartbeat_ms);

	/* age rất lớn = heartbeat vừa được cập nhật giữa hai lần đọc */
	if (age <= SAFETY_DEADLINE_MS || age > INT32_MAX) {
		return;
	}

	motor_emergency_cut();

	if (atomic_cas(&stalled, 0, 1)) {
		LOG_ERR("[SAFETY] control loop stalled %u ms; motors cut", age);
	}
}

static K_TIMER_DEFINE(monitor_timer, monitor_expired, NULL);

void safety_heartbeat(void)
{
	atomic_set(&last_heartbeat_ms, (atomic_val_t)k_uptime_get_32());
	atomic_clear(&stalled);
	(void)wdt_feed(watchdog, watchdog_channel);
}

int safety_start(void)
{
	const struct wdt_timeout_cfg config = {
		.window = {.min = 0U, .max = SAFETY_WATCHDOG_MS},
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!device_is_ready(watchdog)) {
		return -ENODEV;
	}

	watchdog_channel = wdt_install_timeout(watchdog, &config);
	if (watchdog_channel < 0) {
		return watchdog_channel;
	}

	atomic_set(&last_heartbeat_ms, (atomic_val_t)k_uptime_get_32());

	const int ret = wdt_setup(watchdog, 0);

	if (ret != 0) {
		return ret;
	}

	k_timer_start(&monitor_timer, K_MSEC(MONITOR_PERIOD_MS), K_MSEC(MONITOR_PERIOD_MS));

	return 0;
}
