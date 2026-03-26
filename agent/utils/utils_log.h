#pragma once
#include "esp_log.h"

// 在文件开头定义日志级别
#define LEAN_LOG_LEVEL 3 // 0:NONE, 1:ERROR, 2:WARN, 3:INFO, 4:DEBUG

// 根据级别控制日志输出
#if LEAN_LOG_LEVEL >= 4
#define LEAN_DEBUG(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LEAN_DEBUG(tag, format, ...)
#endif

#if LEAN_LOG_LEVEL >= 3
#define LEAN_INFO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LEAN_INFO(tag, format, ...)
#endif

#if LEAN_LOG_LEVEL >= 2
#define LEAN_WARN(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#else
#define LEAN_WARN(tag, format, ...)
#endif

#if LEAN_LOG_LEVEL >= 1
#define LEAN_ERROR(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#else
#define LEAN_ERROR(tag, format, ...)
#endif