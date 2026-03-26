#pragma once
#include "llm_access.h"

typedef struct {
  /**
   * @brief 创建适配器的句柄
   *
   */
  void* (*create)(const lean_llm_access_config* config);

  /**
   * @brief 大模型api的url
   *
   */
  const char* (*url)(void* hd, const lean_llm_access_config* config);

  /**
   * @brief post参数ata的编码
   *
   */
  char* (*encode)(void* hd, const lean_llm_access_config* config, const lean_llm_access_message* message, int counts, lean_llm_error_code* err_code);

  /**
   * @brief 返回数据的解码，需要返回ai的答复内容
   *
   */
  lean_llm_access_message* (*decode)(void* hd, const lean_llm_access_config* config, const char* rsp, const int len, int* op_counts, lean_llm_error_code* err_code);

  /**
   * @brief 数据释放
   *
   */
  void (*release)(void* hd);
  void (*release_encode)(void* ptr);
  void (*release_decode)(lean_llm_access_message* ptr);
} lean_llm_access_adapter;

#define LLM_ADAPTER_EXPORT(name)     const lean_llm_access_adapter const g_access_##name##_adapter
#define LLM_ADAPTER_IMPORT(name)     extern const lean_llm_access_adapter const g_access_##name##_adapter
#define LLM_ADAPTER_APPEND(id, name) [id] = &g_access_##name##_adapter

/**
 * @brief 获取对应的适配器
 * 
 * @param type 
 * @return const lean_llm_access_adapter* 
 */
const lean_llm_access_adapter *lean_llm_access_adapter_get(lean_llm_access_type type);