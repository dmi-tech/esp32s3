/*
 * Phát WAV 16 kHz mono nhúng trong firmware qua I2S0 -> MAX98357.
 * Driver I2S của ESP32 chỉ nhận 2 kênh nên mỗi mẫu được nhân đôi.
 */

#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>

#include "audio.h"
#include "robot_state.h"
#include "wav.h"

LOG_MODULE_REGISTER(audio, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)

/* Thấp hơn luồng điều khiển, cao hơn web để không hụt dữ liệu */
#define AUDIO_PRIORITY K_PRIO_PREEMPT(8)
#define AUDIO_STACK_SIZE 3072

#define FRAMES_PER_BLOCK 512
#define BLOCK_BYTES (FRAMES_PER_BLOCK * 2 * sizeof(int16_t))
#define BLOCK_COUNT 8
#define PREFILL_BLOCKS 4
#define I2S_TIMEOUT_MS 500
#define DRAIN_POLL_MS 10

/* Âm lượng 0.30 như SetGain() của bản cũ */
#define GAIN_NUM 3
#define GAIN_DEN 10

#define FILE_NAME_LEN 80

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_NODELABEL(i2s0));
static const struct gpio_dt_spec amp_gain = GPIO_DT_SPEC_GET(USER_NODE, amp_gain_gpios);
static const struct gpio_dt_spec amp_sd = GPIO_DT_SPEC_GET(USER_NODE, amp_sd_gpios);

static const uint8_t welcome_wav[] = {
#include "welcome.wav.inc"
};

/*
 * Slab được khởi tạo lại trước mỗi lần phát: I2S_TRIGGER_DROP của driver ESP32
 * dừng DMA mà không trả block đang truyền, nên mỗi lần bị ngắt sẽ mất một block.
 */
static uint8_t __aligned(4) tx_blocks[BLOCK_COUNT * BLOCK_BYTES];
static struct k_mem_slab tx_slab;

static K_SEM_DEFINE(request_sem, 0, 1);
static struct k_spinlock request_lock;
static char requested[FILE_NAME_LEN];

static K_THREAD_STACK_DEFINE(audio_stack, AUDIO_STACK_SIZE);
static struct k_thread audio_data;

void audio_request(const char *file)
{
	const k_spinlock_key_t key = k_spin_lock(&request_lock);

	robot_copy_text(requested, sizeof(requested), file);
	k_spin_unlock(&request_lock, key);

	k_sem_give(&request_sem);
}

/* Chỉ có một đoạn âm thanh; tên .mp3 cũ vẫn được chấp nhận cho app.js */
static bool clip_known(const char *name)
{
	if (strncmp(name, "/audio/", 7) == 0) {
		name += 7;
	}

	return strcmp(name, "welcome.mp3") == 0 || strcmp(name, "welcome.wav") == 0;
}

/* Cấp một block và điền mẫu bắt đầu từ *offset, phần thiếu là im lặng */
static int queue_block(const struct wav_pcm *clip, size_t *offset)
{
	void *block;
	int ret = k_mem_slab_alloc(&tx_slab, &block, K_MSEC(I2S_TIMEOUT_MS));

	if (ret != 0) {
		return ret;
	}

	int16_t *samples = block;

	memset(block, 0, BLOCK_BYTES);

	for (size_t i = 0; i < FRAMES_PER_BLOCK && *offset < clip->bytes; i++) {
		const int16_t raw = (int16_t)sys_get_le16(&clip->data[*offset]);
		const int16_t value = (int16_t)(raw * GAIN_NUM / GAIN_DEN);

		samples[2 * i] = value;
		samples[2 * i + 1] = value;
		*offset += sizeof(int16_t);
	}

	ret = i2s_write(i2s_dev, block, BLOCK_BYTES);
	if (ret != 0) {
		k_mem_slab_free(&tx_slab, block);
	}

	return ret;
}

/* 0 = phát xong, 1 = bị yêu cầu mới ngắt, âm = lỗi */
static int play(const struct wav_pcm *clip)
{
	struct i2s_config config = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER,
		.frame_clk_freq = clip->sample_rate,
		.mem_slab = &tx_slab,
		.block_size = BLOCK_BYTES,
		.timeout = I2S_TIMEOUT_MS,
	};
	size_t offset = 0;
	/* DMA đã dừng (DROP ở lần trước), không còn ai giữ block */
	int ret = k_mem_slab_init(&tx_slab, tx_blocks, BLOCK_BYTES, BLOCK_COUNT);

	if (ret != 0) {
		return ret;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &config);

	if (ret != 0) {
		return ret;
	}

	for (int i = 0; i < PREFILL_BLOCKS && offset < clip->bytes; i++) {
		ret = queue_block(clip, &offset);
		if (ret != 0) {
			return ret;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		return ret;
	}

	while (offset < clip->bytes) {
		if (k_sem_count_get(&request_sem) > 0) {
			return 1;
		}
		ret = queue_block(clip, &offset);
		if (ret != 0) {
			return ret;
		}
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret != 0) {
		return ret;
	}

	/* Chờ DMA trả hết block */
	for (int waited = 0; k_mem_slab_num_used_get(&tx_slab) > 0U; waited += DRAIN_POLL_MS) {
		if (waited >= I2S_TIMEOUT_MS) {
			return -ETIMEDOUT;
		}
		k_msleep(DRAIN_POLL_MS);
	}

	return 0;
}

static void audio_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct wav_pcm clip;

	if (wav_parse(welcome_wav, sizeof(welcome_wav), &clip) != 0) {
		LOG_ERR("[AUDIO] embedded WAV is invalid");
		return;
	}

	while (true) {
		char name[FILE_NAME_LEN];

		(void)k_sem_take(&request_sem, K_FOREVER);

		const k_spinlock_key_t key = k_spin_lock(&request_lock);

		robot_copy_text(name, sizeof(name), requested);
		k_spin_unlock(&request_lock, key);

		LOG_INF("[AUDIO] request %s", name);

		if (!clip_known(name)) {
			robot_set_noticef("Audio not found: %s", name);
			LOG_WRN("[AUDIO] not found %s", name);
			continue;
		}

		robot_set_noticef("Playing %s", name);

		const int ret = play(&clip);

		/* Đưa driver về READY cho lần sau, kể cả sau lỗi hụt dữ liệu */
		(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);

		if (ret == 0) {
			robot_set_notice("Audio done");
		} else if (ret < 0) {
			robot_set_notice("Audio begin FAILED");
			LOG_ERR("[AUDIO] playback failed: %d", ret);
		}
	}
}

int audio_init(void)
{
	if (!device_is_ready(i2s_dev) || !gpio_is_ready_dt(&amp_gain) ||
	    !gpio_is_ready_dt(&amp_sd)) {
		return -ENODEV;
	}

	/* Trình tự của setup() bản cũ: SD LOW, GAIN LOW, rồi SD HIGH */
	int ret = gpio_pin_configure_dt(&amp_sd, GPIO_OUTPUT_INACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&amp_gain, GPIO_OUTPUT_INACTIVE);
	}
	if (ret == 0) {
		ret = gpio_pin_set_dt(&amp_sd, 1);
	}
	if (ret != 0) {
		return ret;
	}

	k_tid_t tid = k_thread_create(&audio_data, audio_stack,
				      K_THREAD_STACK_SIZEOF(audio_stack), audio_loop, NULL,
				      NULL, NULL, AUDIO_PRIORITY, 0, K_NO_WAIT);

	(void)k_thread_name_set(tid, "audio");

	return 0;
}
