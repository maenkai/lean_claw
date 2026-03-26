#pragma once
#include "agent_core.h"
#include "esp_err.h"

typedef void* lean_feishu_bot_handle_t;

/**
 * @brief 创建一个飞书机器人
 *
 * @param agent 接入的智能体
 * @param app_id 飞书的appid
 * @param app_secret 飞书的appsecret
 * @return lean_feishu_bot_ctx_t*
 */
lean_feishu_bot_handle_t lean_feishu_bot_create(lean_agent_handle agent, const char* app_id, const char* app_secret);

/**
 * @brief 启动飞书机器人
 *
 * @param bot
 * @return true
 * @return false
 */
bool lean_feishu_bot_start(lean_feishu_bot_handle_t bot);

/**
 * @brief 销毁一个飞书机器人
 *
 * @param bot
 */
void lean_feishu_bot_destroy(lean_feishu_bot_handle_t* bot);

/**
 * @brief 发送信息给对应的用户
 *
 * @param bot
 * @param chat_id
 * @param text
 * @return esp_err_t
 */
esp_err_t lean_feishu_bot_send_message(lean_feishu_bot_handle_t bot, const char* chat_id, const char* text);

/**
 * @brief 回复应用用户的信息
 *
 * @param bot
 * @param message_id
 * @param text
 * @return esp_err_t
 */
esp_err_t lean_feishu_bot_reply_message(lean_feishu_bot_handle_t bot, const char* message_id, const char* text);