#include <stdarg.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "events.h"

LOG_MODULE_REGISTER(events, LOG_LEVEL_INF);

#define EVENT_QUEUE_LEN 16

K_MSGQ_DEFINE(event_queue, EVENT_MESSAGE_LEN, EVENT_QUEUE_LEN, 4);

static atomic_t config_requested;

void event_log(const char *fmt, ...)
{
	char message[EVENT_MESSAGE_LEN];
	va_list args;

	va_start(args, fmt);
	(void)vsnprintk(message, sizeof(message), fmt, args);
	va_end(args);

	LOG_INF("%s", message);
	(void)k_msgq_put(&event_queue, message, K_NO_WAIT);
}

bool event_take(char message[EVENT_MESSAGE_LEN])
{
	return k_msgq_get(&event_queue, message, K_NO_WAIT) == 0;
}

void event_request_config(void)
{
	atomic_set(&config_requested, 1);
}

bool event_take_config_request(void)
{
	return atomic_clear(&config_requested) != 0;
}
