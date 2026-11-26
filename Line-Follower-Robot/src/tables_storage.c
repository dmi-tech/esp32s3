/* tables.json trên LittleFS, phân vùng "storage" */

#include <errno.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "destination_manager.h"
#include "tables_storage.h"

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

#define MOUNT_POINT "/lfs"
#define TABLES_PATH MOUNT_POINT "/tables.json"
#define TABLES_TMP_PATH MOUNT_POINT "/tables.tmp"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_config);

static struct fs_mount_t storage_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_config,
	.storage_dev = (void *)PARTITION_ID(storage_partition),
	.mnt_point = MOUNT_POINT,
};

int tables_storage_mount(void)
{
	/* Phân vùng chưa có filesystem thì được định dạng, như LittleFS.begin(true) */
	const int ret = fs_mount(&storage_mount);

	if (ret != 0) {
		LOG_ERR("[FS] LittleFS mount FAILED: %d", ret);
	} else {
		LOG_INF("[FS] LittleFS OK");
	}

	return ret;
}

int tables_storage_read(char *buf, size_t size)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	int ret = fs_stat(TABLES_PATH, &entry);

	if (ret != 0) {
		return ret;
	}
	if (entry.size > size) {
		return -EFBIG;
	}

	fs_file_t_init(&file);
	ret = fs_open(&file, TABLES_PATH, FS_O_READ);
	if (ret != 0) {
		return ret;
	}

	const ssize_t got = fs_read(&file, buf, entry.size);

	(void)fs_close(&file);

	if (got < 0) {
		return (int)got;
	}

	return (size_t)got == entry.size ? (int)got : -EIO;
}

/* Ghi ra file tạm rồi đổi tên, mất điện giữa chừng không hỏng bảng cũ */
int tables_storage_write(const char *data, size_t len)
{
	struct fs_file_t file;

	fs_file_t_init(&file);

	int ret = fs_open(&file, TABLES_TMP_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);

	if (ret != 0) {
		return ret;
	}

	const ssize_t put = fs_write(&file, data, len);

	ret = fs_close(&file);

	if (put < 0 || (size_t)put != len) {
		ret = put < 0 ? (int)put : -ENOSPC;
	}
	if (ret == 0) {
		ret = fs_rename(TABLES_TMP_PATH, TABLES_PATH);
	}
	if (ret != 0) {
		(void)fs_unlink(TABLES_TMP_PATH);
	}

	return ret;
}
