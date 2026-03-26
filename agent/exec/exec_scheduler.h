#pragma once
#include "nvs.h"
#include "nvs_flash.h"
#include "skill.h"
#include "cJSON.h"

typedef void* lean_scheduler_node;
typedef void (*lean_scheduler_finish_cb)(int id, cJSON* res);

/**
 * @brief 创建定时节点
 *
 * @param skill     定时使用的skill
 * @param finish_cb 完成时的回调
 * @return lean_scheduler_node
 */
lean_scheduler_node lean_scheduler_create_node(lean_skill_handle skill, lean_scheduler_finish_cb finish_cb);

/**
 * @brief 解析json并且创建定时任务
 *
 * @param node 添加到的节点
 * @param timer
 * @param skill
 * @param op_result
 */
void lean_scheduler_handle_json_cmd(lean_scheduler_node node, cJSON* timer, cJSON* op_result);

/**
 * @brief 添加定时任务
 *
 * @param node 定时器节点
 * @param wdays 星期几 bitmask (0x01=周一，0x40=周日，0x7F=每天)
 * @param daytime_seconds 当天触发时间秒数 (0~86399)
 * @param func 执行的函数配置 JSON
 * @return uint32_t 定时任务 ID，0 表示失败
 */
uint32_t lean_scheduler_add(lean_scheduler_node node, bool once, uint8_t wdays, uint32_t daytime_seconds, cJSON* func);

/**
 * @brief 删除定时任务
 *
 * @param node 定时器节点
 * @param id 定时任务 ID
 */
void lean_scheduler_delete(lean_scheduler_node node, uint32_t id);

/**
 * @brief 获取定时列表 JSON
 *
 * @param node 定时器节点
 * @return cJSON* 定时列表 JSON 数组
 */
cJSON* lean_scheduler_get_list_to_json(lean_scheduler_node node);

/**
 * @brief 销毁定时器节点
 *
 * @param node 定时器节点
 */
void lean_scheduler_destroy(lean_scheduler_node node);