#pragma once
#include "executor.h"

/**
 * @brief 工具合集配置获取
 *
 * @return lean_skill_config
 */
const lean_skill_config* tool_collection_config_get(void);

/**
 * @brief 工具合集数量获取
 *
 * @return int
 */
int tool_collection_counts_get(void);

/**
 * @brief 工具运行合集
 *
 * @param input
 * @param output
 * @param prov_data
 * @return true
 * @return false
 */
bool tool_collection_exec(const lean_exec_ctx* msg, const lean_exec_input* input, lean_exec_output* output, void* prov_data);