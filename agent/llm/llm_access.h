#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  LLM_ACCESS_TYPE_DEEPSEEK,
  LLM_ACCESS_TYPE_CHATGPT,
  LLM_ACCESS_TYPE_QWEN,
  //...
  LLM_ACCESS_TYPE_MAX
} lean_llm_access_type;

typedef enum {
  LLM_ACCESS_ROLE_SYSTEM,
  LLM_ACCESS_ROLE_USER,
  LLM_ACCESS_ROLE_ASSISTANT
} lean_llm_access_role;

typedef enum {
  LLM_ERROR_NONE = 0,
  LLM_ERROR_MEMORY_ALLOCATION_FAILED,
  LLM_ERROR_HTTP_REQUEST_FAILED,
  LLM_ERROR_RESPONSE_DECODING_FAILED,
  LLM_ERROR_TOKEN_EXHAUSTED
} lean_llm_error_code;

typedef struct {
  lean_llm_access_role role;
  const char*          content;
} lean_llm_access_message;

typedef void* lean_llm_access_handle;
typedef void (*lean_llm_on_msg_rsp_cb)(lean_llm_error_code err_code, lean_llm_access_message* message, int counts, void* priv_data);

/*@brief Configuration structure for creating an LLM access instance */
typedef struct {
  const char* api_key;
  const char* model;
} lean_llm_access_config;

/**
 * @brief
 *
 * @param type
 * @param config
 * @return lean_llm_access_handle
 */
lean_llm_access_handle lean_llm_access_create(lean_llm_access_type type, const lean_llm_access_config* config);

/**
 * @brief 注册回复的函数回调
 *
 * @param cb
 */
void lean_llm_access_set_rsp_cb_register(lean_llm_access_handle hd, lean_llm_on_msg_rsp_cb cb);

/**
 * @brief
 *
 * @param hd
 * @param message
 * @param counts
 * @param priv_data
 */
void lean_llm_access_send_message(lean_llm_access_handle hd, const lean_llm_access_message* message, int counts, void* priv_data);

/**
 * @brief
 *
 * @param hd
 */
void lean_llm_access_release(lean_llm_access_handle hd);