#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_TEXT_LEN 6

/*
 * Chuỗi cho TM1637: "HHMM", giây chẵn thêm '.' sau giờ (bật dấu hai chấm).
 * Chưa đồng bộ giờ thì "----". local_seconds đã cộng múi giờ.
 */
void clock_format(bool valid, int64_t local_seconds, char out[CLOCK_TEXT_LEN]);
