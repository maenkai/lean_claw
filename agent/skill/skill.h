#pragma once
#include <stdint.h>
#include <stdbool.h>

// 参数封装
#define skill_param(...) "[" #__VA_ARGS__ "]"

typedef void* lean_skill_handle;

typedef struct {
  uint32_t    id;
  const char* desc;
  const char* param;
  const char* ret;
} lean_skill_config;

/**
 * @brief 创建skill表
 *
 * @return lean_skill_handle
 */
lean_skill_handle lean_skill_create(const char *desc);

/**
 * @brief 添加skill
 *
 * @param hd
 * @param config
 * @example
 *  config = {
 *  id = 1...max 不可重复
 *  desc = “open file”
 *  param = skill_param(int#整形,bool#布尔,str#字符串,f#浮点值)
 *  ret = "返回描述"
 * }
 */
void lean_skill_append(lean_skill_handle hd, const lean_skill_config* config, uint32_t coutns);

/**
 * @brief 获取skill的json对象数据
 *
 * @param hd
 * @return cJSON*
 */
char* lean_skill_get_jsonstring(lean_skill_handle hd);
