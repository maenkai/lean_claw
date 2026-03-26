#include "utils_dynamic_buffer.h"
#include "stdlib.h"
#include "string.h"

/**
 * @brief 初始化buffer
 *
 * @param buf
 */
void lean_utils_dynamic_buffer_init(lean_utils_dynamic_buffer* buf, size_t capacity) {
  buf->buffer   = NULL;
  buf->length   = 0;
  buf->capacity = 0;

  if (capacity) {
    buf->buffer   = malloc(capacity);
    buf->capacity = buf->buffer ? capacity : 0;
  }
}

/**
 * @brief 向buffer追加数据，如果当前容量不足以容纳新数据，则自动扩展容量
 *
 * @param buf
 * @param data
 * @param data_len
 * @return true
 * @return false
 */
bool lean_utils_dynamic_buffer_append(lean_utils_dynamic_buffer* buf, const void* data, size_t data_len) {
  if (buf->length + data_len > buf->capacity) {
    size_t new_capacity = (buf->capacity == 0) ? 64 : buf->capacity * 2;
    while (new_capacity < buf->length + data_len) {
      new_capacity *= 2;
    }
    char* new_buffer = (char*)realloc(buf->buffer, new_capacity);
    if (new_buffer == NULL) {
      return false;
    }
    buf->buffer   = new_buffer;
    buf->capacity = new_capacity;
  }
  memcpy(buf->buffer + buf->length, data, data_len);
  buf->length += data_len;
  return true;
}

/**
 * @brief 释放buffer
 *
 * @param buf
 */
void lean_utils_dynamic_buffer_free(lean_utils_dynamic_buffer* buf) {
  if (buf->buffer) {
    free(buf->buffer);
    buf->buffer   = NULL;
    buf->length   = 0;
    buf->capacity = 0;
  }
}

/**
 * @brief 获取数据指针
 *
 * @param buf
 * @return void*
 */
void* lean_utils_dynamic_buffer_get_data(const lean_utils_dynamic_buffer* buf) {
  return buf->buffer;
}

/**
 * @brief 获取数据长度
 *
 * @param buf
 * @return size_t
 */
size_t lean_utils_dynamic_buffer_get_length(const lean_utils_dynamic_buffer* buf) {
  return buf->length;
}

/**
 * @brief 获取数据容量
 *
 * @param buf
 * @return size_t
 */
size_t lean_utils_dynamic_buffer_get_capacity(const lean_utils_dynamic_buffer* buf) {
  return buf->capacity;
}