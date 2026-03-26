#include "llm_access.h"
#include "llm_adapter.h"
#include "utils_dynamic_buffer.h"
#include "utils_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"

#include "stdlib.h"

#define TAG                  "llm_access"
#define API_QUERY_TIMEOUT_MS 10000

typedef struct {
  void*                          user_data;
  void*                          apt_hd;
  const lean_llm_access_adapter* apt_hook;
  lean_llm_access_config         config;
  lean_llm_on_msg_rsp_cb         rsp_cb;
  lean_utils_dynamic_buffer      buffer;
} _lean_llm_access_handle;

/**
 * @brief 返回str错误码
 *
 * @param code
 * @return const char*
 */
static const char* lean_llm_error_code_to_str(lean_llm_error_code code) {
  static const char* error_str[] = {
    "LLM_ERROR_NONE",
    "LLM_ERROR_MEMORY_ALLOCATION_FAILED",
    "LLM_ERROR_HTTP_REQUEST_FAILED",
    "LLM_ERROR_RESPONSE_DECODING_FAILED",
    "LLM_ERROR_TOKEN_EXHAUSTED",
  };

  if (code >= (sizeof(error_str) / sizeof(const char*))) {
    return "LLM_ERROR_UNKNOWN";
  }

  return error_str[code];
}

/**
 * @brief 处理大模型的回复数据，并回调给上层
 *
 * @param access
 * @param data
 * @param len
 */
static void lean_llm_access_notify_response(const _lean_llm_access_handle* access, void* data, size_t len) {
  lean_llm_error_code            err_code = LLM_ERROR_NONE;
  const lean_llm_access_adapter* adapter  = access->apt_hook;

  int                      counts  = 0;
  lean_llm_access_message* content = adapter->decode(access->apt_hd, &access->config, data, len, &counts, &err_code);

  if (err_code != LLM_ERROR_NONE) {
    LEAN_ERROR(TAG, "llm response failed , err code = (%d)%s", err_code, lean_llm_error_code_to_str(err_code));
  }

  if (access->rsp_cb) {
    access->rsp_cb(err_code, content, counts, access->user_data);
  }
  adapter->release_decode(content);
}

/**
 * @brief
 *
 * @param event
 * @return esp_err_t
 */
static esp_err_t on_llm_http_response(esp_http_client_event_t* event) {
  _lean_llm_access_handle* access = (_lean_llm_access_handle*)event->user_data;

  switch (event->event_id) {
    case HTTP_EVENT_ON_DATA:
      LEAN_DEBUG(TAG, "HTTP_EVENT_ON_DATA");
      lean_utils_dynamic_buffer_append(&access->buffer, event->data, event->data_len);
      break;

    case HTTP_EVENT_ON_FINISH:
      LEAN_DEBUG(TAG, "HTTP_EVENT_ON_FINISH");
      lean_llm_access_notify_response(access, lean_utils_dynamic_buffer_get_data(&access->buffer),
                                      lean_utils_dynamic_buffer_get_length(&access->buffer));
      lean_utils_dynamic_buffer_free(&access->buffer);
      break;

    case HTTP_EVENT_DISCONNECTED:
      LEAN_DEBUG(TAG, "HTTP_EVENT_DISCONNECTED");
      lean_utils_dynamic_buffer_free(&access->buffer);
      break;

    case HTTP_EVENT_ERROR:
      LEAN_ERROR(TAG, "HTTP_EVENT_ERROR");
      break;

    default:
      break;
  }
  return ESP_OK;
}

/**
 * @brief
 *
 * @param type
 * @param config
 * @return lean_llm_access_handle
 */
lean_llm_access_handle lean_llm_access_create(lean_llm_access_type type, const lean_llm_access_config* config) {
  _lean_llm_access_handle* hd = (_lean_llm_access_handle*)malloc(sizeof(_lean_llm_access_handle));
  hd->config                  = *config;
  hd->apt_hook                = lean_llm_access_adapter_get(type);
  hd->apt_hd                  = hd->apt_hook->create(config);
  return hd;
}

/**
 * @brief 注册回复的函数回调
 *
 * @param cb
 */
void lean_llm_access_set_rsp_cb_register(lean_llm_access_handle hd, lean_llm_on_msg_rsp_cb cb) {
  _lean_llm_access_handle* access_hd = (_lean_llm_access_handle*)hd;
  access_hd->rsp_cb                  = cb;
}

/**
 * @brief
 *
 * @param hd
 * @param system_msg
 * @param user_msg
 */
void lean_llm_access_send_message(lean_llm_access_handle hd, const lean_llm_access_message* message, int counts, void* priv_data) {
  _lean_llm_access_handle*       access   = (_lean_llm_access_handle*)hd;
  lean_llm_error_code            err_code = LLM_ERROR_NONE;
  const lean_llm_access_adapter* adapter  = access->apt_hook;
  const char*                    api_url  = adapter->url(access->apt_hd, &access->config);
  access->user_data                       = priv_data;
  lean_utils_dynamic_buffer_init(&access->buffer, 0);

  esp_http_client_config_t config = {
    .url               = api_url,
    .event_handler     = on_llm_http_response,
    .timeout_ms        = API_QUERY_TIMEOUT_MS,
    .method            = HTTP_METHOD_POST,
    .transport_type    = HTTP_TRANSPORT_OVER_SSL,
    .crt_bundle_attach = esp_crt_bundle_attach,
    .user_data         = access,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  char  authorization[128] = { 0 };
  char* json_format        = adapter->encode(access->apt_hd, &access->config, message, counts, &err_code);

  LEAN_DEBUG(TAG, "http post raw: %s", json_format);
  sprintf(authorization, "Bearer %s", access->config.api_key);
  esp_http_client_set_header(client, "Authorization", authorization);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json_format, strlen(json_format));
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    LEAN_ERROR(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
  adapter->release_encode(json_format);
}

/**
 * @brief 释放资源
 *
 * @param hd
 */
void lean_llm_access_release(lean_llm_access_handle hd) {
  if (hd == NULL) {
    return;
  }
  _lean_llm_access_handle* access = (_lean_llm_access_handle*)hd;
  access->apt_hook->release(access->apt_hd);
  free(access);
}