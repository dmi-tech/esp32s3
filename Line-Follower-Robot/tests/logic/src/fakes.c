#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "audio.h"
#include "destination_manager.h"
#include "events.h"
#include "fakes.h"
#include "motor_control.h"
#include "robot_logic.h"

struct fake_hw fake;

void motor_output(int left, int right)
{
	fake.left = left;
	fake.right = right;
}

void stop_motors(void)
{
	fake.left = 0;
	fake.right = 0;
}

void audio_request(const char *file)
{
	fake.audio_requests++;
	robot_copy_text(fake.audio_file, sizeof(fake.audio_file), file);
}

int tables_storage_read(char *buf, size_t size)
{
	if (fake.file_len < 0) {
		return -ENOENT;
	}
	if ((size_t)fake.file_len > size) {
		return -EFBIG;
	}
	memcpy(buf, fake.file, fake.file_len);
	return fake.file_len;
}

int tables_storage_write(const char *data, size_t len)
{
	if (fake.fail_write || len > sizeof(fake.file)) {
		return -EIO;
	}
	memcpy(fake.file, data, len);
	fake.file_len = (int)len;
	return 0;
}

void fake_reset(void)
{
	static struct robot_state defaults;
	static bool saved;
	char message[EVENT_MESSAGE_LEN];

	if (!saved) {
		defaults = g_robot;
		saved = true;
	}

	g_robot = defaults;
	memset(&fake, 0, sizeof(fake));
	fake.file_len = -1;
	k_msgq_purge(&g_cmd_queue);
	while (event_take(message)) {
	}
	(void)event_take_config_request();
	robot_logic_reset();
	(void)destinations_init("{}");
}

bool event_seen(const char *needle)
{
	char message[EVENT_MESSAGE_LEN];
	bool seen = false;

	while (event_take(message)) {
		seen = seen || strstr(message, needle) != NULL;
	}

	return seen;
}
