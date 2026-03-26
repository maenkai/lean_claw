#pragma once

/**
 * @brief 来自终端的聊天输入内容
 *
 */
typedef void (*lean_cmd_chat_pipeline_cb)(const char* data, int length);

/**
 * @brief register_chat
 *
 */
void cmd_register_chat(lean_cmd_chat_pipeline_cb cb);