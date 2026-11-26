#pragma once

int audio_init(void);

/* Yêu cầu phát một file; không chặn, gọi được từ luồng điều khiển. */
void audio_request(const char *file);
