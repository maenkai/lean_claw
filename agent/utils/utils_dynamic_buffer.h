#pragma once
#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

typedef struct {
  void*  buffer;   // 数据缓冲区
  size_t length;   // 当前数据长度
  size_t capacity; // 缓冲区总容量
} lean_utils_dynamic_buffer;

/**
 * @brief 初始化buffer
 *
 * @param buf
 */
void lean_utils_dynamic_buffer_init(lean_utils_dynamic_buffer* buf, size_t capacity);

/**
 * @brief 向buffer追加数据，如果当前容量不足以容纳新数据，则自动扩展容量
 *
 * @param buf
 * @param data
 * @param data_len
 * @return true
 * @return false
 */
bool lean_utils_dynamic_buffer_append(lean_utils_dynamic_buffer* buf, const void* data, size_t data_len);

/**
 * @brief 释放buffer
 *
 * @param buf
 */
void lean_utils_dynamic_buffer_free(lean_utils_dynamic_buffer* buf);

/**
 * @brief 获取数据指针
 *
 * @param buf
 * @return void*
 */
void* lean_utils_dynamic_buffer_get_data(const lean_utils_dynamic_buffer* buf);

/**
 * @brief 获取数据长度
 *
 * @param buf
 * @return size_t
 */
size_t lean_utils_dynamic_buffer_get_length(const lean_utils_dynamic_buffer* buf);

/**
 * @brief 获取数据容量
 *
 * @param buf
 * @return size_t
 */
size_t lean_utils_dynamic_buffer_get_capacity(const lean_utils_dynamic_buffer* buf);