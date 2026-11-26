#include <string.h>

#include "clock_format.h"

#define SECONDS_PER_DAY 86400

void clock_format(bool valid, int64_t local_seconds, char out[CLOCK_TEXT_LEN])
{
	if (!valid || local_seconds < 0) {
		memcpy(out, "----", 5);
		return;
	}

	const int in_day = (int)(local_seconds % SECONDS_PER_DAY);
	const int hour = in_day / 3600;
	const int minute = (in_day / 60) % 60;
	int n = 0;

	out[n++] = (char)('0' + hour / 10);
	out[n++] = (char)('0' + hour % 10);
	if (in_day % 2 == 0) {
		out[n++] = '.';
	}
	out[n++] = (char)('0' + minute / 10);
	out[n++] = (char)('0' + minute % 10);
	out[n] = '\0';
}
