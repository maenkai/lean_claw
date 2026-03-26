#pragma once
#include "llm_access.h"
#include "skill.h"
#include "exec_thread.h"

typedef struct {
  lean_llm_access_handle  llm;
  lean_skill_handle skill;
} lean_agent_core_config;

typedef enum {
  AGENT_CORE_STATE_ON_IDLE, // 空闲
  AGENT_CORE_STATE_ON_BUSY  // 执行中
} lean_agent_core_state;

typedef void* lean_agent_core_handle;
typedef void* lean_agent_core_channel;

/**
 * @brief 消息回复函数回调
 * @param agent 来自的agent
 * @param channel 来自的channel
 * @param msg 回复的消息
 * @param priv_data 回复消息的私有数据
 */
typedef void (*lean_agent_core_llm_rsp_cb)(lean_agent_core_handle agent, const lean_agent_core_channel channel,
                                      const char* msg, void* priv_data);

/**
 * @brief 创建一个智能体
 *
 * @param config
 * @return lean_agent_core_handle
 */
lean_agent_core_handle lean_agent_core_create(lean_agent_core_config* config);

/**
 * @brief 获取智能体当前的状态
 *
 * @param hd
 * @return lean_agent_core_state
 */
lean_agent_core_state lean_agent_core_state_get(lean_agent_core_handle hd);

/**
 * @brief 创建消息通道
 *
 * @param hd 智能体
 * @param name 名称
 * @param cb 接收回调
 * @param sub_all_channel = true时,非回复本通道的消息也能接收到
 * @return lean_agent_core_channel
 */
lean_agent_core_channel lean_agent_core_channel_create(lean_agent_core_handle hd, const char* name, lean_agent_core_llm_rsp_cb user_cb, void* user_data, bool sub_all_channel);

/**
 * @brief 删除消息通道
 *
 * @param ch
 */
void lean_agent_core_channel_delete(lean_agent_core_handle hd, lean_agent_core_channel* ch);

/**
 * @brief 向智能体发送信息
 *
 * @param hd
 * @param channel
 * @param msg
 * @param user_data
 */
void agent_core_send_message(lean_agent_core_handle hd, lean_agent_core_channel channel, const char* msg);

/**
 * @brief 获取智能体的线程节点
 *
 * @param hd
 * @return lean_thread_node
 */
lean_thread_node agent_core_get_thread_node(lean_agent_core_handle hd);