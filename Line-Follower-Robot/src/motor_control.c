#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "motor_control.h"
#include "robot_state.h"

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec sensors[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER_NODE, sensor_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
};

static const struct gpio_dt_spec motor_in[] = {
	DT_FOREACH_PROP_ELEM_SEP(USER_NODE, motor_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
};

BUILD_ASSERT(ARRAY_SIZE(sensors) == 5, "cần đúng 5 cảm biến line");
BUILD_ASSERT(ARRAY_SIZE(motor_in) == 4, "cần đúng IN1..IN4");

struct motor_channel {
	struct pwm_dt_spec enable;
	const struct gpio_dt_spec *forward;
	const struct gpio_dt_spec *reverse;
};

static const struct motor_channel left_motor = {
	.enable = PWM_DT_SPEC_GET_BY_NAME(USER_NODE, ena),
	.forward = &motor_in[0],
	.reverse = &motor_in[1],
};

static const struct motor_channel right_motor = {
	.enable = PWM_DT_SPEC_GET_BY_NAME(USER_NODE, enb),
	.forward = &motor_in[2],
	.reverse = &motor_in[3],
};

static int set_duty(const struct motor_channel *m, int duty)
{
	const uint32_t pulse =
		(uint32_t)(((uint64_t)m->enable.period * (uint32_t)duty) / MOTOR_SPEED_LIMIT);

	return pwm_set_pulse_dt(&m->enable, pulse);
}

static void motor_set(const struct motor_channel *m, int speed)
{
	speed = CLAMP(speed, -MOTOR_SPEED_LIMIT, MOTOR_SPEED_LIMIT);

	int err = gpio_pin_set_dt(m->forward, speed > 0);

	err |= gpio_pin_set_dt(m->reverse, speed < 0);
	err |= set_duty(m, speed < 0 ? -speed : speed);

	if (err != 0) {
		/* An toàn: hai chân IN cùng LOW thì L298N không cấp điện */
		(void)gpio_pin_set_dt(m->forward, 0);
		(void)gpio_pin_set_dt(m->reverse, 0);
		LOG_ERR("motor output failed; bridge disabled");
	}
}

void motor_output(int left, int right)
{
	motor_set(&left_motor, left);
	motor_set(&right_motor, right);
}

void stop_motors(void)
{
	/* Tắt IN trước để motor dừng kể cả khi PWM lỗi */
	for (size_t i = 0; i < ARRAY_SIZE(motor_in); i++) {
		(void)gpio_pin_set_dt(&motor_in[i], 0);
	}

	if ((set_duty(&left_motor, 0) | set_duty(&right_motor, 0)) != 0) {
		LOG_ERR("cannot set PWM to zero");
	}
}

void motor_emergency_cut(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(motor_in); i++) {
		(void)gpio_pin_set_dt(&motor_in[i], 0);
	}
}

int motor_control_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(motor_in); i++) {
		if (!gpio_is_ready_dt(&motor_in[i])) {
			return -ENODEV;
		}

		const int ret = gpio_pin_configure_dt(&motor_in[i], GPIO_OUTPUT_INACTIVE);

		if (ret != 0) {
			return ret;
		}
	}

	if (!pwm_is_ready_dt(&left_motor.enable) || !pwm_is_ready_dt(&right_motor.enable)) {
		return -ENODEV;
	}

	int ret = set_duty(&left_motor, 0);

	if (ret == 0) {
		ret = set_duty(&right_motor, 0);
	}

	return ret;
}

int line_sensors_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		if (!gpio_is_ready_dt(&sensors[i])) {
			return -ENODEV;
		}

		const int ret = gpio_pin_configure_dt(&sensors[i], GPIO_INPUT);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int read_sensors(uint8_t sensor[5])
{
	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		const int level = gpio_pin_get_dt(&sensors[i]);

		if (level < 0) {
			return level;
		}

		sensor[i] = (level == LINE_ON) ? 1U : 0U;
	}

	return 0;
}
