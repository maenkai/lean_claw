#pragma once
#include "skill.h"
#include "cJSON.h"

/**
 * @brief 根据指令集调用skill
 *
 * @param func
 * @param skill
 */
void lean_executor_function_running(cJSON* func, lean_skill_handle skill, cJSON* op_result);
