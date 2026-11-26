#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_IP_LEN 16

/* Chạy luồng WiFi (kết nối, thử lại, SNTP). Không chặn. */
void network_start(void);

/* ip = "0.0.0.0" và rssi = 0 khi chưa kết nối */
void network_status(char ip[NETWORK_IP_LEN], int *rssi);

/* Giờ địa phương (ICT-7) tính bằng giây kể từ epoch; false khi chưa đồng bộ. */
bool network_local_time(int64_t *seconds);
