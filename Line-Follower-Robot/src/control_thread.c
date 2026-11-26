#include <zephyr/kernel.h>

#include "control_thread.h"
#include "motor_control.h"
#include "obstacle_monitor.h"
#include "robot_logic.h"
#include "safety.h"

/*
 * Ưu tiên cooperative cao: cao hơn mạng, web, audio và đồng hồ; mỗi chu kỳ
 * chỉ chạy rất ngắn rồi ngủ.
 */
#define CONTROL_PRIORITY K_PRIO_COOP(2)
#define CONTROL_STACK_SIZE 4096

static K_THREAD_STACK_DEFINE(control_stack, CONTROL_STACK_SIZE);
static struct k_thread control_data;

static void control_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t next_ms = k_uptime_get();

	while (true) {
		uint8_t sensor[5];

		obstacle_monitor_update(k_uptime_get_32());

		if (read_sensors(sensor) != 0) {
			robot_fail_stop("line sensor read failed");
		} else {
			robot_logic_step(k_uptime_get_32(), sensor);
		}

		safety_heartbeat();

		next_ms += LINE_LOOP_DELAY_MS;

		/* Trễ hạn thì lấy mốc mới; không bao giờ chạy liên tục không ngủ */
		const int64_t now_ms = k_uptime_get();

		if (next_ms <= now_ms) {
			next_ms = now_ms + LINE_LOOP_DELAY_MS;
		}

		(void)k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

int control_thread_start(void)
{
	robot_logic_reset();

	/* Không có watchdog thì không cho robot chạy */
	const int ret = safety_start();

	if (ret != 0) {
		return ret;
	}

	k_tid_t tid = k_thread_create(&control_data, control_stack,
				      K_THREAD_STACK_SIZEOF(control_stack), control_loop, NULL,
				      NULL, NULL, CONTROL_PRIORITY, K_FP_REGS, K_NO_WAIT);

	(void)k_thread_name_set(tid, "control");

	return 0;
}
