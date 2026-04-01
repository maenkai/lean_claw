#pragma once
#include "cJSON.h"
#include "executor.h"

/**
 * @brief 定时器配置
 */
typedef struct {
  char*            name;      // 定时器名称
  cJSON*           func;      // 执行的指令数组
  lean_exec_handle exec;      // 技能表
  uint32_t         delay_sec; // 延迟秒数
} lean_timer_config;

/**
 * @brief 创建定时器
 *
 * @param config 配置
 * @return lean_agent_timer_handle
 */
bool lean_agent_timer_create(lean_timer_config* config);

/**
 * @brief 处理 JSON 命令创建定时器
 *
 * @param timer 定时器管理器
 * @param json_item JSON 配置项
 * @param root 返回结果
 */
void lean_agent_timer_handle_json_cmd(lean_exec_handle exec, cJSON* json_item, cJSON* root);