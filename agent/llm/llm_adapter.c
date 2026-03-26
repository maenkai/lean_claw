#include "llm_adapter.h"
#include "stddef.h"

LLM_ADAPTER_IMPORT(qwen);
LLM_ADAPTER_IMPORT(deepseek);
LLM_ADAPTER_IMPORT(chatgpt);

static const lean_llm_access_adapter* s_llm_adapter_hook[LLM_ACCESS_TYPE_MAX] = {
  LLM_ADAPTER_APPEND(LLM_ACCESS_TYPE_QWEN, qwen),
  LLM_ADAPTER_APPEND(LLM_ACCESS_TYPE_DEEPSEEK, deepseek),
  LLM_ADAPTER_APPEND(LLM_ACCESS_TYPE_CHATGPT, chatgpt),
};

/**
 * @brief 获取对应的适配器
 *
 * @param type
 * @return const lean_llm_access_adapter*
 */
const lean_llm_access_adapter* lean_llm_access_adapter_get(lean_llm_access_type type) {
  if (type < 0 || type >= LLM_ACCESS_TYPE_MAX) {
    return NULL;
  }
  return s_llm_adapter_hook[type];
}