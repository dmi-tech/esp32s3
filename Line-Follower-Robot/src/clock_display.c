#include <string.h>

#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "clock_display.h"
#include "clock_format.h"
#include "network.h"

LOG_MODULE_REGISTER(clock, LOG_LEVEL_INF);

/* Driver TM1637 bit-bang chậm nên không chạy trong luồng điều khiển */
#define CLOCK_PRIORITY K_PRIO_PREEMPT(12)
#define CLOCK_STACK_SIZE 2048
#define CLOCK_PERIOD_MS 1000
#define CLOCK_BRIGHTNESS 4

static const struct device *const display = DEVICE_DT_GET(DT_NODELABEL(clock_display));

static K_THREAD_STACK_DEFINE(clock_stack, CLOCK_STACK_SIZE);
static struct k_thread clock_data;

static void clock_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t next_ms = k_uptime_get();

	while (true) {
		char text[CLOCK_TEXT_LEN];
		int64_t seconds = 0;
		const bool valid = network_local_time(&seconds);

		clock_format(valid, seconds, text);

		const int ret =
			auxdisplay_write(display, (const uint8_t *)text, (uint16_t)strlen(text));

		if (ret != 0) {
			LOG_WRN_ONCE("TM1637 write failed: %d", ret);
		}

		next_ms += CLOCK_PERIOD_MS;
		(void)k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

int clock_display_start(void)
{
	if (!device_is_ready(display)) {
		return -ENODEV;
	}

	(void)auxdisplay_brightness_set(display, CLOCK_BRIGHTNESS);

	k_tid_t tid = k_thread_create(&clock_data, clock_stack,
				      K_THREAD_STACK_SIZEOF(clock_stack), clock_loop, NULL,
				      NULL, NULL, CLOCK_PRIORITY, 0, K_NO_WAIT);

	(void)k_thread_name_set(tid, "clock");

	return 0;
}
