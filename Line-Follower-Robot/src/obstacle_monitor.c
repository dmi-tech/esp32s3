/* HC-SR04: TRIG xung 10 us, độ rộng ECHO đo bằng ngắt hai cạnh */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

#include "obstacle_filter.h"
#include "obstacle_monitor.h"
#include "robot_state.h"

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec trig = GPIO_DT_SPEC_GET(USER_NODE, sonar_trig_gpios);
static const struct gpio_dt_spec echo = GPIO_DT_SPEC_GET(USER_NODE, sonar_echo_gpios);
/* Cấu hình input như bản cũ; firmware không đọc chân này */
static const struct gpio_dt_spec out = GPIO_DT_SPEC_GET(USER_NODE, sonar_out_gpios);

static struct gpio_callback echo_callback;
static struct k_spinlock echo_lock;

/* Chia sẻ giữa ISR và luồng điều khiển, bảo vệ bằng echo_lock */
static uint32_t echo_rise_cycles;
static uint32_t echo_pulse_cycles;
static bool echo_high;
static bool echo_ready;

/* Chỉ luồng điều khiển dùng */
static struct obstacle_filter filter;
static bool echo_pending;
static bool has_triggered;
static uint32_t echo_started_ms;
static uint32_t last_trigger_ms;

static void echo_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	const uint32_t now = k_cycle_get_32();
	const bool level = gpio_pin_get_dt(&echo) > 0;
	const k_spinlock_key_t key = k_spin_lock(&echo_lock);

	if (level) {
		echo_rise_cycles = now;
		echo_high = true;
	} else if (echo_high) {
		echo_pulse_cycles = now - echo_rise_cycles;
		echo_high = false;
		echo_ready = true;
	}

	k_spin_unlock(&echo_lock, key);
}

int obstacle_monitor_init(void)
{
	if (!gpio_is_ready_dt(&trig) || !gpio_is_ready_dt(&echo) || !gpio_is_ready_dt(&out)) {
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&trig, GPIO_OUTPUT_INACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&echo, GPIO_INPUT);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&out, GPIO_INPUT);
	}
	if (ret != 0) {
		return ret;
	}

	gpio_init_callback(&echo_callback, echo_isr, BIT(echo.pin));
	ret = gpio_add_callback_dt(&echo, &echo_callback);
	if (ret != 0) {
		return ret;
	}

	return gpio_pin_interrupt_configure_dt(&echo, GPIO_INT_EDGE_BOTH);
}

static void trigger_ping(void)
{
	const k_spinlock_key_t key = k_spin_lock(&echo_lock);

	echo_ready = false;
	echo_high = false;
	k_spin_unlock(&echo_lock, key);

	(void)gpio_pin_set_dt(&trig, 0);
	k_busy_wait(2);
	(void)gpio_pin_set_dt(&trig, 1);
	k_busy_wait(10);
	(void)gpio_pin_set_dt(&trig, 0);
}

void obstacle_monitor_update(uint32_t now_ms)
{
	const k_spinlock_key_t key = k_spin_lock(&echo_lock);
	const bool has_echo = echo_ready;
	const uint32_t pulse_cycles = echo_pulse_cycles;

	echo_ready = false;
	k_spin_unlock(&echo_lock, key);

	if (has_echo) {
		echo_pending = false;
		obstacle_filter_echo(&filter, k_cyc_to_us_floor32(pulse_cycles));
	} else if (echo_pending && now_ms - echo_started_ms >= OBSTACLE_ECHO_TIMEOUT_MS) {
		echo_pending = false;
		obstacle_filter_no_echo(&filter);
	}

	if (!echo_pending &&
	    (!has_triggered || now_ms - last_trigger_ms >= OBSTACLE_PING_INTERVAL_MS)) {
		trigger_ping();
		echo_pending = true;
		has_triggered = true;
		echo_started_ms = now_ms;
		last_trigger_ms = now_ms;
	}

	robot_state_lock();
	g_robot.obstacle_detected = filter.detected;
	g_robot.obstacle_distance_cm = filter.distance_cm;
	robot_state_unlock();
}
