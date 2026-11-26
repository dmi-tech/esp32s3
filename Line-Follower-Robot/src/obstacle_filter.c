#include "obstacle_filter.h"
#include "robot_config.h"

void obstacle_filter_no_echo(struct obstacle_filter *f)
{
	f->distance_cm = 0.0f;
	if (f->detected && ++f->missed >= OBSTACLE_NO_ECHO_CLEAR_COUNT) {
		f->detected = false;
		f->missed = 0;
	}
}

void obstacle_filter_echo(struct obstacle_filter *f, uint32_t pulse_us)
{
	const float distance_cm = (float)pulse_us * 0.0343f / 2.0f;

	if (distance_cm <= 0.0f || distance_cm > 400.0f) {
		obstacle_filter_no_echo(f);
		return;
	}

	f->missed = 0;
	f->distance_cm = distance_cm;

	if (!f->detected && distance_cm < OBSTACLE_STOP_CM) {
		f->detected = true;
	} else if (f->detected && distance_cm >= OBSTACLE_CLEAR_CM) {
		f->detected = false;
	}
}
