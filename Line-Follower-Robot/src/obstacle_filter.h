#pragma once

/* Hysteresis khoảng cách, logic thuần. */

#include <stdbool.h>
#include <stdint.h>

struct obstacle_filter {
	bool detected;
	float distance_cm;
	uint8_t missed;
};

void obstacle_filter_echo(struct obstacle_filter *f, uint32_t pulse_us);
void obstacle_filter_no_echo(struct obstacle_filter *f);
