#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include "wav.h"

#define RIFF_HEADER_LEN 12U
#define CHUNK_HEADER_LEN 8U
#define FMT_CHUNK_LEN 16U

int wav_parse(const uint8_t *file, size_t len, struct wav_pcm *out)
{
	size_t pos = RIFF_HEADER_LEN;
	uint32_t rate = 0;

	if (len < RIFF_HEADER_LEN || memcmp(file, "RIFF", 4) != 0 ||
	    memcmp(&file[8], "WAVE", 4) != 0) {
		return -EINVAL;
	}

	while (len - pos >= CHUNK_HEADER_LEN) {
		const uint8_t *chunk = &file[pos];
		const size_t size = sys_get_le32(&chunk[4]);
		const uint8_t *body = &chunk[CHUNK_HEADER_LEN];

		if (size > len - pos - CHUNK_HEADER_LEN) {
			return -EINVAL;
		}

		if (memcmp(chunk, "fmt ", 4) == 0) {
			if (size < FMT_CHUNK_LEN || sys_get_le16(&body[0]) != 1U /* PCM */ ||
			    sys_get_le16(&body[2]) != 1U /* mono */ ||
			    sys_get_le16(&body[14]) != 16U) {
				return -EINVAL;
			}
			rate = sys_get_le32(&body[4]);
		} else if (memcmp(chunk, "data", 4) == 0) {
			if (rate == 0U || size < 2U) {
				return -EINVAL;
			}
			out->data = body;
			out->bytes = size & ~(size_t)1;
			out->sample_rate = rate;
			return 0;
		}

		/* Chunk lẻ byte được đệm thêm 1 byte */
		const size_t step = CHUNK_HEADER_LEN + size + (size & 1U);

		if (step > len - pos) {
			break;
		}
		pos += step;
	}

	return -EINVAL;
}
