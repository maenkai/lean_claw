#pragma once
#include "cJSON.h"
#include "executor.h"

typedef void* lean_thread_node;

/**
 * @brief Create a thread node object
 *
 * @return lean_thread_node
 */
lean_thread_node lean_thread_node_create(lean_exec_handle exec);

/**
 * @brief agent_executor_thread_create
 *
 * @param thrrad
 * @param skill
 * @param op_result
 */
void lean_thread_handle_json_cmd(lean_thread_node node, cJSON* thread, cJSON* op_result);

/**
 * @brief 添加线程，执行func,返回任务id
 *
 * @param node
 * @param name
 * @param loop
 * @param stack_size
 * @param msleep
 * @param func
 * @return int
 */
int lean_thread_add(lean_thread_node node, const char* name, bool loop, uint32_t stack_size,
                    uint32_t priorty, uint32_t msleep, cJSON* func);

/**
 * @brief 通过id 获取删除线程
 *
 * @param node
 * @param id
 * @return int
 */
void lean_thread_delete(lean_thread_node node, int id);

/**
 * @brief 通过json获取线程列表
 *
 * @param node
 * @return cJSON*
 */
cJSON* lean_thread_get_list_to_json(lean_thread_node thread_node);