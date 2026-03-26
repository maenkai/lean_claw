#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h"
#include "string.h"

#include "apt_qwen.h"
#include "llm_adapter.h"
#include "utils_log.h"

#define TAG          "qwen"
#define EXAMPLE_DATA "{\
    \"model\": \"qwen-turbo\",\
    \"input\":{\
        \"messages\":[\
            {\
                \"role\": \"system\",\
                \"content\": \"You are a helpful assistant.\"\
            },\
            {\
                \"role\": \"user\",\
                \"content\": \"你好，潮州有什么好玩？\"\
            }\
        ]\
    },\
    \"parameters\": {\
        \"result_format\": \"message\"\
    }\
}"

static const char* qwen_role_enum_string[] = {
  [LLM_ACCESS_ROLE_SYSTEM]    = "system",
  [LLM_ACCESS_ROLE_USER]      = "user",
  [LLM_ACCESS_ROLE_ASSISTANT] = "assistant"
};

static lean_llm_access_role qwen_get_role_enum(const char* role_str) {
  for (int i = 0; i < sizeof(qwen_role_enum_string) / sizeof(qwen_role_enum_string[0]); i++) {
    if (strcmp(role_str, qwen_role_enum_string[i]) == 0) {
      return (lean_llm_access_role)i;
    }
  }
  return LLM_ACCESS_ROLE_USER; // 默认返回用户角色
}

static const char* on_qwen_adapter_url(void* hd, const lean_llm_access_config* config) {
  return "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
}

static void* on_qwen_adapter_create(const lean_llm_access_config* config) {
  cJSON* root       = cJSON_CreateObject();
  cJSON* input      = cJSON_CreateObject();
  cJSON* message    = cJSON_CreateArray();
  cJSON* parameters = cJSON_CreateObject();
  cJSON_AddItemToObject(input, "messages", message);
  cJSON_AddStringToObject(parameters, "result_format", "message");
  cJSON_AddStringToObject(root, "model", config->model);
  cJSON_AddItemToObject(root, "input", input);
  cJSON_AddItemToObject(root, "parameters", parameters);
  return root;
}

static char* on_qwen_adapter_encode(void* hd, const lean_llm_access_config* config, const lean_llm_access_message* chat_array, int counts, lean_llm_error_code* err_code) {
  cJSON* root    = (cJSON*)hd;
  cJSON* history = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "input"), "messages");

  for (int i = 0; i < counts; i++) {
    const lean_llm_access_message* chat      = &chat_array[i];
    cJSON*                         chat_item = cJSON_CreateObject();
    cJSON_AddStringToObject(chat_item, "role", qwen_role_enum_string[chat->role]);
    cJSON_AddStringToObject(chat_item, "content", chat->content);
    cJSON_AddItemToArray(history, chat_item);
  }

  char* json_format = cJSON_PrintUnformatted(root);
  return json_format;
}

static lean_llm_access_message* on_qwen_adapter_decode(void* hd, const lean_llm_access_config* config, const char* rsp, const int len, int* output_count, lean_llm_error_code* err_code) {
  LEAN_DEBUG(TAG, "Raw response: %.*s", len, rsp);
  cJSON* history  = cJSON_GetObjectItem(cJSON_GetObjectItem((cJSON*)hd, "input"), "messages");
  cJSON* rsp_json = cJSON_ParseWithLength(rsp, len);
  cJSON* code     = cJSON_GetObjectItem(rsp_json, "code");
  cJSON* output   = cJSON_GetObjectItem(rsp_json, "output");
  cJSON* choices  = cJSON_GetObjectItem(output, "choices");
  cJSON* message  = NULL;

  if (code && !strcmp(code->valuestring, "Arrearage")) {
    *err_code = LLM_ERROR_TOKEN_EXHAUSTED;
    LEAN_ERROR(TAG, "qwen failed: %.*s", len, rsp);
    cJSON_Delete(rsp_json);
    return NULL;
  }

  for (int i = 0; i < cJSON_GetArraySize(choices); i++) {
    cJSON* choice = cJSON_GetArrayItem(choices, i);
    message       = cJSON_GetObjectItem(choice, "message");
    if (NULL != message) {
      break;
    }
  }

  if (NULL == message) {
    *err_code = LLM_ERROR_RESPONSE_DECODING_FAILED;
    LEAN_ERROR(TAG, "qwen failed: %.*s", len, rsp);
    cJSON_Delete(rsp_json);
    return NULL;
  }

  // 增加到历史会话中, 并且将message转换为lean_llm_access_message返回
  lean_llm_access_message* result_message = (lean_llm_access_message*)malloc(sizeof(lean_llm_access_message) * 1);
  cJSON*                   copy_item      = cJSON_Duplicate(message, true);
  result_message->role                    = qwen_get_role_enum(cJSON_GetObjectItem(copy_item, "role")->valuestring);
  result_message->content                 = cJSON_GetObjectItem(copy_item, "content")->valuestring;
  cJSON_AddItemToArray(history, copy_item);
  cJSON_Delete(rsp_json);
  *output_count = 1;
  return result_message;
}

static void on_qwen_adapter_release(void* hd) {
  if (hd) {
    cJSON_Delete((cJSON*)hd);
  }
}

static void on_qwen_adapter_release_encode(void* ptr) {
  if (ptr) {
    cJSON_free(ptr);
  }
}

static void on_qwen_adapter_release_decode(lean_llm_access_message* ptr) {
  if (ptr) {
    free(ptr);
  }
}

LLM_ADAPTER_EXPORT(qwen) = {
  .create         = on_qwen_adapter_create,
  .url            = on_qwen_adapter_url,
  .encode         = on_qwen_adapter_encode,
  .decode         = on_qwen_adapter_decode,
  .release        = on_qwen_adapter_release,
  .release_encode = on_qwen_adapter_release_encode,
  .release_decode = on_qwen_adapter_release_decode,
};