#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "robot_config.h"

#define DESTINATION_MAX 32
/* Đủ cho DESTINATION_MAX mục dạng "2147483647":"<90 ký tự>", */
#define DESTINATION_JSON_LEN 4096

/* error trỏ tới chuỗi hằng, giữ nguyên thông điệp của bản cũ */
bool route_path_valid(const char *path, const char **error);

/* Nạp bảng; chưa có file thì ghi bảng mặc định. */
int destinations_init(const char *default_json);

/* Nạp lại từ bộ nhớ. Lỗi thì giữ nguyên bảng đang có. */
int destinations_load(void);

/* Kiểm tra, ghi xuống bộ nhớ rồi mới thay bảng đang dùng */
int destinations_save_json(const char *body, size_t len, const char **error);

/* Trả về độ dài JSON, hoặc mã âm nếu bộ đệm nhỏ */
int destinations_json(char *out, size_t out_len);

bool destinations_lookup(int id, char code[ROUTE_CODE_MAX_LEN + 1]);

/*
 * Lớp lưu trữ (tables_storage.c; test thay bằng bản trong RAM).
 * read trả về số byte hoặc mã âm (-ENOENT nếu chưa có file).
 */
int tables_storage_read(char *buf, size_t size);
int tables_storage_write(const char *data, size_t len);
