#pragma once

#include <stddef.h>
#include <stdint.h>

struct wav_pcm {
	const uint8_t *data; /* mẫu 16 bit little-endian, mono */
	size_t bytes;
	uint32_t sample_rate;
};

/* Chỉ nhận WAV PCM 16 bit mono. Trả về 0 hoặc -EINVAL. */
int wav_parse(const uint8_t *file, size_t len, struct wav_pcm *out);
