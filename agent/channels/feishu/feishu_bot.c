#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"

#include "feishu_bot.h"
#include "agent_core.h"
#include "utils_dynamic_buffer.h"
#include "utils_log.h"

#define FEISHU_API_BASE      "https://open.feishu.cn/open-apis"
#define FEISHU_AUTH_URL      FEISHU_API_BASE "/auth/v3/tenant_access_token/internal"
#define FEISHU_SEND_MSG_URL  FEISHU_API_BASE "/im/v1/messages"
#define FEISHU_REPLY_MSG_URL FEISHU_API_BASE "/im/v1/messages/%s/reply"
#define FEISHU_WS_CONFIG_URL "https://open.feishu.cn/callback/ws/endpoint"

#define FEISHU_THREAD_PRIORITY       (9 | portPRIVILEGE_BIT)
#define FEISHU_THREAD_STACK_SIZE     (4 * 1024)
#define FEISHU_THREAD_PINNED_TO_CORE (1)

#define FEISHU_WS_THREAD_SIZE    (4 * 1024)
#define FEISHU_WS_TX_BUFFER_SIZE (2048)

#define FEISHU_SESSION_CACHE_SIZE (32)
#define FEISHU_DEDUP_CACHE_SIZE   (64)

static const char* TAG = "feishu_bot";

typedef struct {
  char key[32];
  char value[128];
} ws_header_t;

typedef struct {
  uint64_t       seq_id;
  uint64_t       log_id;
  int32_t        service;
  int32_t        method;
  ws_header_t    headers[16];
  size_t         header_count;
  const uint8_t* payload;
  size_t         payload_len;
} ws_frame_t;

typedef struct {
  lean_agent_channel channel;
  char               chat_id[64];
  char               msg_id[64];
  uint32_t           access_time;
} session_item_t;

typedef struct {
  /* Credentials & token state */
  char    app_id[64];
  char    app_secret[128];
  char    tenant_token[512];
  int64_t token_expire_time;

  /* WebSocket state */
  esp_websocket_client_handle_t ws_client;
  TaskHandle_t                  ws_task;
  char*                         ws_url;
  int                           ws_ping_interval_ms;
  int                           ws_reconnect_interval_ms;
  int                           ws_reconnect_nonce_ms;
  int                           ws_service_id;
  bool                          ws_connected;
  lean_utils_dynamic_buffer     ws_buffer;
  ws_frame_t                    ws_ping;

  /* Agent Core integration */
  lean_agent_handle agent_core;

  /* Session cache */
  session_item_t* session_cache;
  size_t          session_cache_size;
  size_t          session_idx;

  /* Message deduplication */
  uint64_t* seen_msg_keys;
  size_t    seen_msg_keys_size;
  size_t    seen_msg_idx;
} lean_feishu_bot_ctx_t;

static esp_err_t on_http_response(esp_http_client_event_t* evt) {
  lean_utils_dynamic_buffer* buf = (lean_utils_dynamic_buffer*)evt->user_data;
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    lean_utils_dynamic_buffer_append(buf, evt->data, evt->data_len);
  }
  return ESP_OK;
}

static bool pb_read_varint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
  uint64_t v     = 0;
  int      shift = 0;
  while (*pos < len && shift <= 63) {
    uint8_t b = buf[(*pos)++];
    v |= ((uint64_t)(b & 0x7F)) << shift;
    if ((b & 0x80) == 0) {
      *out = v;
      return true;
    }
    shift += 7;
  }
  return false;
}

static bool pb_skip_field(const uint8_t* buf, size_t len, size_t* pos, uint8_t wire_type) {
  uint64_t n = 0;
  switch (wire_type) {
    case 0:
      return pb_read_varint(buf, len, pos, &n);
    case 1:
      if (*pos + 8 > len)
        return false;
      *pos += 8;
      return true;
    case 2:
      if (!pb_read_varint(buf, len, pos, &n))
        return false;
      if (*pos + (size_t)n > len)
        return false;
      *pos += (size_t)n;
      return true;
    case 5:
      if (*pos + 4 > len)
        return false;
      *pos += 4;
      return true;
    default:
      return false;
  }
}

static bool pb_parse_header_msg(const uint8_t* buf, size_t len, ws_header_t* h) {
  memset(h, 0, sizeof(*h));
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, slen = 0;
    if (!pb_read_varint(buf, len, &pos, &tag))
      return false;
    uint32_t field = (uint32_t)(tag >> 3);
    uint8_t  wt    = (uint8_t)(tag & 0x07);
    if (wt != 2) {
      if (!pb_skip_field(buf, len, &pos, wt))
        return false;
      continue;
    }
    if (!pb_read_varint(buf, len, &pos, &slen))
      return false;
    if (pos + (size_t)slen > len)
      return false;
    if (field == 1) {
      size_t n = (slen < sizeof(h->key) - 1) ? (size_t)slen : sizeof(h->key) - 1;
      memcpy(h->key, buf + pos, n);
      h->key[n] = '\0';
    } else if (field == 2) {
      size_t n = (slen < sizeof(h->value) - 1) ? (size_t)slen : sizeof(h->value) - 1;
      memcpy(h->value, buf + pos, n);
      h->value[n] = '\0';
    }
    pos += (size_t)slen;
  }
  return true;
}

static bool pb_parse_frame(const uint8_t* buf, size_t len, ws_frame_t* f) {
  memset(f, 0, sizeof(*f));
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, v = 0, blen = 0;
    if (!pb_read_varint(buf, len, &pos, &tag))
      return false;
    uint32_t field = (uint32_t)(tag >> 3);
    uint8_t  wt    = (uint8_t)(tag & 0x07);
    if (field == 1 && wt == 0) {
      if (!pb_read_varint(buf, len, &pos, &f->seq_id))
        return false;
    } else if (field == 2 && wt == 0) {
      if (!pb_read_varint(buf, len, &pos, &f->log_id))
        return false;
    } else if (field == 3 && wt == 0) {
      if (!pb_read_varint(buf, len, &pos, &v))
        return false;
      f->service = (int32_t)v;
    } else if (field == 4 && wt == 0) {
      if (!pb_read_varint(buf, len, &pos, &v))
        return false;
      f->method = (int32_t)v;
    } else if (field == 5 && wt == 2) {
      if (!pb_read_varint(buf, len, &pos, &blen))
        return false;
      if (pos + (size_t)blen > len)
        return false;
      if (f->header_count < 16) {
        pb_parse_header_msg(buf + pos, (size_t)blen, &f->headers[f->header_count++]);
      }
      pos += (size_t)blen;
    } else if (field == 8 && wt == 2) {
      if (!pb_read_varint(buf, len, &pos, &blen))
        return false;
      if (pos + (size_t)blen > len)
        return false;
      f->payload     = buf + pos;
      f->payload_len = (size_t)blen;
      pos += (size_t)blen;
    } else {
      if (!pb_skip_field(buf, len, &pos, wt))
        return false;
    }
  }
  return true;
}

static const char* frame_header_value(const ws_frame_t* f, const char* key) {
  for (size_t i = 0; i < f->header_count; i++) {
    if (strcmp(f->headers[i].key, key) == 0) {
      return f->headers[i].value;
    }
  }
  return NULL;
}

static uint64_t fnv1a64(const char* s) {
  uint64_t h = 1469598103934665603ULL;
  if (!s)
    return h;
  while (*s) {
    h ^= (unsigned char)(*s++);
    h *= 1099511628211ULL;
  }
  return h;
}

static bool dedup_check_and_record(lean_feishu_bot_ctx_t* ctx, const char* message_id) {
  uint64_t key = fnv1a64(message_id);
  for (size_t i = 0; i < ctx->seen_msg_keys_size; i++) {
    if (ctx->seen_msg_keys[i] == key)
      return true;
  }
  ctx->seen_msg_keys[ctx->seen_msg_idx] = key;
  ctx->seen_msg_idx                     = (ctx->seen_msg_idx + 1) % ctx->seen_msg_keys_size;
  return false;
}

static const char* session_find_chat_id(lean_feishu_bot_ctx_t* ctx, lean_agent_channel channel) {
  for (size_t i = 0; i < ctx->session_cache_size; i++) {
    if (ctx->session_cache[i].channel == channel) {
      return ctx->session_cache[i].chat_id;
    }
  }
  return NULL;
}

static void session_agent_response(lean_agent_handle agent, lean_agent_channel channel, const char* msg, void* priv_data) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)priv_data;
  if (!ctx)
    return;

  const char* chat_id = session_find_chat_id(ctx, channel);
  if (!chat_id) {
    LEAN_WARN(TAG, "Channel not found in session cache");
    return;
  }

  const char* msg_id = NULL;
  for (size_t i = 0; i < ctx->session_cache_size; i++) {
    if (ctx->session_cache[i].channel == channel) {
      msg_id = ctx->session_cache[i].msg_id;
      break;
    }
  }

  esp_err_t ret;
  if (msg_id && msg_id[0]) {
    ret = lean_feishu_bot_reply_message(ctx, msg_id, msg);
  } else {
    ret = lean_feishu_bot_send_message(ctx, chat_id, msg);
  }

  if (ret != ESP_OK) {
    LEAN_ERROR(TAG, "Failed to send response to Feishu");
  }
}

static session_item_t* session_get_or_create(lean_feishu_bot_ctx_t* ctx, const char* chat_id) {
  for (size_t i = 0; i < ctx->session_cache_size; i++) {
    if (ctx->session_cache[i].channel != NULL && strcmp(ctx->session_cache[i].chat_id, chat_id) == 0) {
      ctx->session_cache[i].access_time = (uint32_t)esp_timer_get_time();
      return &ctx->session_cache[i];
    }
  }

  char channel_name[16];
  snprintf(channel_name, sizeof(channel_name), "fs_%.8s", chat_id);

  lean_agent_channel ch = lean_agent_channel_create(
    ctx->agent_core,
    channel_name,
    session_agent_response,
    ctx,
    false);

  if (ch == NULL) {
    LEAN_ERROR(TAG, "Failed to create channel for %s", chat_id);
    return NULL;
  }

  session_item_t* entry = &ctx->session_cache[ctx->session_idx];
  lean_agent_channel_delete(ctx->agent_core, &entry->channel);
  strncpy(entry->chat_id, chat_id, sizeof(entry->chat_id) - 1);
  entry->channel     = ch;
  entry->access_time = (uint32_t)esp_timer_get_time();
  ctx->session_idx   = (ctx->session_idx + 1) % ctx->session_cache_size;
  return entry;
}

static bool feishu_get_tenant_token(lean_feishu_bot_ctx_t* ctx) {
  if (ctx->app_id[0] == '\0' || ctx->app_secret[0] == '\0') {
    LEAN_WARN(TAG, "No Feishu credentials configured");
    return false;
  }

  int64_t now = esp_timer_get_time() / 1000000LL;
  if (ctx->tenant_token[0] != '\0' && ctx->token_expire_time > now + 300) {
    return true;
  }

  cJSON* body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "app_id", ctx->app_id);
  cJSON_AddStringToObject(body, "app_secret", ctx->app_secret);
  char* json_str = cJSON_PrintUnformatted(body);
  cJSON_Delete(body);
  if (!json_str)
    return false;

  lean_utils_dynamic_buffer rsp = { 0 };
  lean_utils_dynamic_buffer_init(&rsp, 2048);

  esp_http_client_config_t config = {
    .url               = FEISHU_AUTH_URL,
    .event_handler     = on_http_response,
    .user_data         = &rsp,
    .timeout_ms        = 10000,
    .buffer_size       = 2048,
    .buffer_size_tx    = 2048,
    .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(json_str);
    lean_utils_dynamic_buffer_free(&rsp);
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, json_str, strlen(json_str));

  esp_err_t err = esp_http_client_perform(client);
  esp_http_client_cleanup(client);
  free(json_str);

  if (err != ESP_OK) {
    LEAN_ERROR(TAG, "Token request HTTP failed: %s", esp_err_to_name(err));
    lean_utils_dynamic_buffer_free(&rsp);
    return false;
  }

  cJSON* root = cJSON_ParseWithLength(
    (const char*)lean_utils_dynamic_buffer_get_data(&rsp),
    lean_utils_dynamic_buffer_get_length(&rsp));
  lean_utils_dynamic_buffer_free(&rsp);

  if (!root) {
    LEAN_ERROR(TAG, "Failed to parse token response");
    return false;
  }

  cJSON* code = cJSON_GetObjectItem(root, "code");
  if (!code || code->valueint != 0) {
    LEAN_ERROR(TAG, "Token request failed: code=%d", code ? code->valueint : -1);
    cJSON_Delete(root);
    return false;
  }

  cJSON* token  = cJSON_GetObjectItem(root, "tenant_access_token");
  cJSON* expire = cJSON_GetObjectItem(root, "expire");

  if (token && cJSON_IsString(token)) {
    strncpy(ctx->tenant_token, token->valuestring, sizeof(ctx->tenant_token) - 1);
    ctx->token_expire_time = now + (expire ? expire->valueint : 7200) - 300;
  }

  cJSON_Delete(root);
  return true;
}

static bool feishu_api_call(lean_feishu_bot_ctx_t* ctx, const char* url,
                            const char* method, const char* post_data,
                            lean_utils_dynamic_buffer* output) {
  if (!feishu_get_tenant_token(ctx)) {
    return false;
  }

  lean_utils_dynamic_buffer rsp = { 0 };
  lean_utils_dynamic_buffer_init(&rsp, 4096);

  esp_http_client_config_t config = {
    .url               = url,
    .event_handler     = on_http_response,
    .user_data         = &rsp,
    .timeout_ms        = 15000,
    .buffer_size       = 2048,
    .buffer_size_tx    = 2048,
    .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client      = NULL;
  size_t                   header_size = strlen(ctx->tenant_token) + strlen("Authorization: Bearer ") + 1;
  char*                    auth_header = malloc(header_size);

  if (NULL == auth_header) {
    goto failed;
  }

  client = esp_http_client_init(&config);
  if (!client) {
    goto failed;
  }

  int size = snprintf(auth_header, header_size, "Bearer %s", ctx->tenant_token);
  if (size <= 0) {
    goto failed;
  }

  auth_header[size] = '\0';
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");

  if (strcmp(method, "POST") == 0) {
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (post_data) {
      esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    LEAN_ERROR(TAG, "API call failed: %s", esp_err_to_name(err));
    goto failed;
  }

  esp_http_client_cleanup(client);
  free(auth_header);
  *output = rsp;
  return true;

failed:
  lean_utils_dynamic_buffer_free(&rsp);
  if (auth_header) {
    free(auth_header);
  }

  if (client) {
    esp_http_client_cleanup(client);
  }

  return false;
}
static bool http_post_ws_config(lean_feishu_bot_ctx_t* ctx) {
  cJSON* object = cJSON_CreateObject();
  cJSON_AddStringToObject(object, "AppID", ctx->app_id);
  cJSON_AddStringToObject(object, "AppSecret", ctx->app_secret);
  char* json_str = cJSON_PrintUnformatted(object);
  cJSON_Delete(object);

  if (!json_str) {
    return false;
  }

  lean_utils_dynamic_buffer rsp = { 0 };
  lean_utils_dynamic_buffer_init(&rsp, 4096);

  esp_http_client_config_t config = {
    .url               = FEISHU_WS_CONFIG_URL,
    .event_handler     = on_http_response,
    .user_data         = &rsp,
    .timeout_ms        = 15000,
    .buffer_size       = 2048,
    .buffer_size_tx    = 1024,
    .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(json_str);
    lean_utils_dynamic_buffer_free(&rsp);
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "locale", "zh");
  esp_http_client_set_post_field(client, json_str, strlen(json_str));
  esp_err_t err    = esp_http_client_perform(client);
  int       status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  free(json_str);

  if (err != ESP_OK || status != 200) {
    LEAN_ERROR(TAG, "WS config request failed: err=%s http=%d",
               esp_err_to_name(err), status);
    lean_utils_dynamic_buffer_free(&rsp);
    return false;
  }

  cJSON* root = cJSON_ParseWithLength(
    (const char*)lean_utils_dynamic_buffer_get_data(&rsp),
    lean_utils_dynamic_buffer_get_length(&rsp));
  lean_utils_dynamic_buffer_free(&rsp);

  if (!root)
    return false;

  cJSON* code = cJSON_GetObjectItem(root, "code");
  cJSON* data = cJSON_GetObjectItem(root, "data");
  cJSON* url  = data ? cJSON_GetObjectItem(data, "URL") : NULL;
  cJSON* ccfg = data ? cJSON_GetObjectItem(data, "ClientConfig") : NULL;

  if (!code || code->valueint != 0 || !url || !cJSON_IsString(url)) {
    LEAN_ERROR(TAG, "Invalid WS config response");
    cJSON_Delete(root);
    return false;
  }

  free(ctx->ws_url);
  ctx->ws_url = strdup(url->valuestring);

  if (ccfg) {
    cJSON* pi = cJSON_GetObjectItem(ccfg, "PingInterval");
    cJSON* ri = cJSON_GetObjectItem(ccfg, "ReconnectInterval");
    cJSON* rn = cJSON_GetObjectItem(ccfg, "ReconnectNonce");
    if (pi && cJSON_IsNumber(pi))
      ctx->ws_ping_interval_ms = pi->valueint * 1000;
    if (ri && cJSON_IsNumber(ri))
      ctx->ws_reconnect_interval_ms = ri->valueint * 1000;
    if (rn && cJSON_IsNumber(rn))
      ctx->ws_reconnect_nonce_ms = rn->valueint * 1000;
  }

  cJSON_Delete(root);
  return true;
}

/* ── WebSocket Frame Encoding ─────────────────────────────── */
static bool pb_write_varint(uint8_t* buf, size_t cap, size_t* pos, uint64_t value) {
  do {
    if (*pos >= cap)
      return false;
    uint8_t byte = (uint8_t)(value & 0x7F);
    value >>= 7;
    if (value)
      byte |= 0x80;
    buf[(*pos)++] = byte;
  } while (value);
  return true;
}

static bool pb_write_tag(uint8_t* buf, size_t cap, size_t* pos, uint32_t field, uint8_t wt) {
  return pb_write_varint(buf, cap, pos, ((uint64_t)field << 3) | wt);
}

static bool pb_write_bytes(uint8_t* buf, size_t cap, size_t* pos, uint32_t field,
                           const uint8_t* data, size_t len) {
  if (!pb_write_tag(buf, cap, pos, field, 2))
    return false;
  if (!pb_write_varint(buf, cap, pos, len))
    return false;
  if (*pos + len > cap)
    return false;
  memcpy(buf + *pos, data, len);
  *pos += len;
  return true;
}

static bool pb_write_string(uint8_t* buf, size_t cap, size_t* pos,
                            uint32_t field, const char* s) {
  return pb_write_bytes(buf, cap, pos, field, (const uint8_t*)s, strlen(s));
}

static bool ws_encode_header(uint8_t* dst, size_t cap, size_t* out_len,
                             const char* key, const char* value) {
  size_t pos = 0;
  if (!pb_write_string(dst, cap, &pos, 1, key))
    return false;
  if (!pb_write_string(dst, cap, &pos, 2, value))
    return false;
  *out_len = pos;
  return true;
}

static void ws_send_frame(lean_feishu_bot_ctx_t* ctx, const ws_frame_t* f,
                          const uint8_t* payload, size_t payload_len, int timeout_ms) {
  uint8_t* out = malloc(FEISHU_WS_TX_BUFFER_SIZE);
  size_t   pos = 0;
  if (!pb_write_tag(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 1, 0) || !pb_write_varint(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, f->seq_id)) {
    return;
  }

  if (!pb_write_tag(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 2, 0) || !pb_write_varint(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, f->log_id)) {
    return;
  }

  if (!pb_write_tag(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 3, 0) || !pb_write_varint(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, (uint32_t)f->service)) {
    return;
  }

  if (!pb_write_tag(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 4, 0) || !pb_write_varint(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, (uint32_t)f->method)) {
    return;
  }

  for (size_t i = 0; i < f->header_count; i++) {
    uint8_t hb[256];
    size_t  hlen = 0;
    if (!ws_encode_header(hb, sizeof(hb), &hlen,
                          f->headers[i].key, f->headers[i].value))
      return;
    if (!pb_write_bytes(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 5, hb, hlen))
      return;
  }
  if (payload && payload_len > 0) {
    if (!pb_write_bytes(out, FEISHU_WS_TX_BUFFER_SIZE, &pos, 8, payload, payload_len))
      return;
  }

  int sent_len = esp_websocket_client_send_bin(ctx->ws_client, (const char*)out, pos, timeout_ms);
  free(out);
  if (sent_len != pos) {
    ESP_LOGE(TAG, "Binary data sent successfully (%d bytes)", sent_len);
  }
}

esp_err_t lean_feishu_bot_send_message(lean_feishu_bot_handle_t bot, const char* chat_id, const char* text) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)bot;
  if (!ctx || !chat_id || !text)
    return ESP_ERR_INVALID_ARG;
  if (ctx->app_id[0] == '\0' || ctx->app_secret[0] == '\0') {
    LEAN_WARN(TAG, "Cannot send: no credentials configured");
    return ESP_ERR_INVALID_STATE;
  }

  const char* id_type = "chat_id";
  if (strncmp(chat_id, "ou_", 3) == 0) {
    id_type = "open_id";
  }

  char url[256];
  snprintf(url, sizeof(url), "%s?receive_id_type=%s", FEISHU_SEND_MSG_URL, id_type);

  cJSON* content = cJSON_CreateObject();
  cJSON_AddStringToObject(content, "text", text);
  char* content_str = cJSON_PrintUnformatted(content);
  cJSON_Delete(content);
  if (!content_str)
    return ESP_ERR_NO_MEM;

  cJSON* body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "receive_id", chat_id);
  cJSON_AddStringToObject(body, "msg_type", "text");
  cJSON_AddStringToObject(body, "content", content_str);
  free(content_str);

  char* json_str = cJSON_PrintUnformatted(body);
  cJSON_Delete(body);
  if (!json_str)
    return ESP_ERR_NO_MEM;

  lean_utils_dynamic_buffer rsp = { 0 };
  feishu_api_call(ctx, url, "POST", json_str, &rsp);
  free(json_str);

  if (NULL == lean_utils_dynamic_buffer_get_data(&rsp)) {
    return ESP_FAIL;
  }

  esp_err_t ret  = ESP_FAIL;
  cJSON*    root = cJSON_ParseWithLength(lean_utils_dynamic_buffer_get_data(&rsp),
                                         lean_utils_dynamic_buffer_get_length(&rsp));
  lean_utils_dynamic_buffer_free(&rsp);

  if (NULL == root) {
    return ret;
  }

  cJSON* code = cJSON_GetObjectItem(root, "code");
  if (code && code->valueint == 0) {
    ret = ESP_OK;
  } else {
    cJSON* msg = cJSON_GetObjectItem(root, "msg");
    LEAN_WARN(TAG, "Send failed: code=%d, msg=%s",
              code ? code->valueint : -1, msg ? msg->valuestring : "unknown");
  }
  cJSON_Delete(root);
  return ret;
}

esp_err_t lean_feishu_bot_reply_message(lean_feishu_bot_handle_t bot, const char* message_id, const char* text) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)bot;

  if (!ctx || !message_id || !text)
    return ESP_ERR_INVALID_ARG;
  if (ctx->app_id[0] == '\0' || ctx->app_secret[0] == '\0') {
    return ESP_ERR_INVALID_STATE;
  }

  char url[256];
  snprintf(url, sizeof(url), FEISHU_REPLY_MSG_URL, message_id);

  cJSON* content = cJSON_CreateObject();
  cJSON_AddStringToObject(content, "text", text);
  char* content_str = cJSON_PrintUnformatted(content);
  cJSON_Delete(content);
  if (!content_str)
    return ESP_ERR_NO_MEM;

  cJSON* body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "msg_type", "text");
  cJSON_AddStringToObject(body, "content", content_str);
  free(content_str);

  char* json_str = cJSON_PrintUnformatted(body);
  cJSON_Delete(body);
  if (!json_str)
    return ESP_ERR_NO_MEM;

  lean_utils_dynamic_buffer rsp = { 0 };
  feishu_api_call(ctx, url, "POST", json_str, &rsp);
  free(json_str);

  if (NULL == lean_utils_dynamic_buffer_get_data(&rsp)) {
    return ESP_FAIL;
  }

  esp_err_t ret  = ESP_FAIL;
  cJSON*    root = cJSON_ParseWithLength(lean_utils_dynamic_buffer_get_data(&rsp),
                                         lean_utils_dynamic_buffer_get_length(&rsp));
  lean_utils_dynamic_buffer_free(&rsp);

  if (NULL == root) {
    return ret;
  }

  cJSON* code = cJSON_GetObjectItem(root, "code");
  if (code && code->valueint == 0) {
    ret = ESP_OK;
  } else {
    cJSON* msg = cJSON_GetObjectItem(root, "msg");
    LEAN_WARN(TAG, "Reply failed: code=%d, msg=%s",
              code ? code->valueint : -1, msg ? msg->valuestring : "unknown");
  }
  cJSON_Delete(root);
  return ret;
}

static void session_feishu_message_recv(lean_feishu_bot_ctx_t* ctx, cJSON* event) {
  cJSON* message = cJSON_GetObjectItem(event, "message");
  if (!message)
    return;

  cJSON* message_id_j = cJSON_GetObjectItem(message, "message_id");
  cJSON* chat_id_j    = cJSON_GetObjectItem(message, "chat_id");
  cJSON* chat_type_j  = cJSON_GetObjectItem(message, "chat_type");
  cJSON* msg_type_j   = cJSON_GetObjectItem(message, "message_type");
  cJSON* content_j    = cJSON_GetObjectItem(message, "content");

  if (!chat_id_j || !cJSON_IsString(chat_id_j))
    return;
  if (!content_j || !cJSON_IsString(content_j))
    return;

  const char* message_id = cJSON_IsString(message_id_j) ? message_id_j->valuestring : "";
  const char* chat_id    = chat_id_j->valuestring;
  const char* chat_type  = cJSON_IsString(chat_type_j) ? chat_type_j->valuestring : "p2p";
  const char* msg_type   = cJSON_IsString(msg_type_j) ? msg_type_j->valuestring : "text";

  if (message_id[0] && dedup_check_and_record(ctx, message_id)) {
    LEAN_DEBUG(TAG, "Duplicate message %s, skipping", message_id);
    return;
  }

  if (strcmp(msg_type, "text") != 0) {
    LEAN_INFO(TAG, "Ignoring non-text message type: %s", msg_type);
    return;
  }

  cJSON* content_obj = cJSON_Parse(content_j->valuestring);
  if (!content_obj) {
    LEAN_WARN(TAG, "Failed to parse message content JSON");
    return;
  }

  cJSON* text_j = cJSON_GetObjectItem(content_obj, "text");
  if (!text_j || !cJSON_IsString(text_j)) {
    cJSON_Delete(content_obj);
    return;
  }

  const char* text    = text_j->valuestring;
  const char* cleaned = text;
  if (strncmp(cleaned, "@_user_1 ", 9) == 0) {
    cleaned += 9;
  }
  while (*cleaned == ' ' || *cleaned == '\n')
    cleaned++;

  if (cleaned[0] == '\0') {
    cJSON_Delete(content_obj);
    return;
  }

  const char* sender_id = "";
  cJSON*      sender    = cJSON_GetObjectItem(event, "sender");
  if (sender) {
    cJSON* sender_id_obj = cJSON_GetObjectItem(sender, "sender_id");
    if (sender_id_obj) {
      cJSON* open_id = cJSON_GetObjectItem(sender_id_obj, "open_id");
      if (open_id && cJSON_IsString(open_id)) {
        sender_id = open_id->valuestring;
      }
    }
  }

  const char* route_id = chat_id;
  if (strcmp(chat_type, "p2p") == 0 && sender_id[0]) {
    route_id = sender_id;
  }

  session_item_t* session = session_get_or_create(ctx, route_id);
  if (session == NULL) {
    LEAN_ERROR(TAG, "Failed to get session for %s", route_id);
    cJSON_Delete(content_obj);
    return;
  }

  // 连续发内容的话就不引用,变成正常回复
  if (lean_agent_state_get(ctx->agent_core) == AGENT_CORE_STATE_ON_IDLE) {
    strncpy(session->msg_id, message_id, sizeof(session->msg_id) - 1);
    lean_agent_send_message(ctx->agent_core, session->channel, cleaned);
  } else {
    lean_feishu_bot_reply_message(ctx, message_id, "claw 忙碌中, 请稍后重试");
  }

  cJSON_Delete(content_obj);
}

static void feishu_ws_handle_message(lean_feishu_bot_ctx_t* ctx, const uint8_t* rx_buf, size_t rx_len) {
  ws_frame_t* frame = malloc(sizeof(ws_frame_t));
  if (NULL == frame) {
    LEAN_ERROR(TAG, "Allocate frame failed.");
    return;
  }

  if (!pb_parse_frame(rx_buf, rx_len, frame)) {
    LEAN_ERROR(TAG, "parse frame error");
    free(frame);
    return;
  }

  const char* type = frame_header_value(frame, "type");
  if (frame->method == 0) {
    if (type && strcmp(type, "ping") == 0 && frame->payload && frame->payload_len > 0) {
      cJSON* cfg = cJSON_ParseWithLength((const char*)frame->payload, frame->payload_len);
      if (cfg) {
        cJSON* pi = cJSON_GetObjectItem(cfg, "PingInterval");
        if (pi && cJSON_IsNumber(pi))
          ctx->ws_ping_interval_ms = pi->valueint * 1000;
        cJSON_Delete(cfg);
      }
    }
  } else if (type && strcmp(type, "event") == 0 && frame->payload && frame->payload_len > 0) {
    cJSON* root = cJSON_ParseWithLength((const char*)frame->payload, frame->payload_len);
    if (root) {
      cJSON* event  = cJSON_GetObjectItem(root, "event");
      cJSON* header = cJSON_GetObjectItem(root, "header");
      if (event && header) {
        cJSON* event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type) && strcmp(event_type->valuestring, "im.message.receive_v1") == 0) {
          session_feishu_message_recv(ctx, event);
        }
      } else if (event) {
        session_feishu_message_recv(ctx, event);
      }
      cJSON_Delete(root);
    }

    char ack[32];
    int  ack_len = snprintf(ack, sizeof(ack), "{\"code\":%d}", 200);
    ws_send_frame(ctx, frame, (const uint8_t*)ack, (size_t)ack_len, 1000);
  }

  free(frame);
}

static void feishu_ws_event_handler(void* arg, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
  lean_feishu_bot_ctx_t*      ctx = (lean_feishu_bot_ctx_t*)arg;
  esp_websocket_event_data_t* e   = (esp_websocket_event_data_t*)event_data;

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ctx->ws_connected = true;
      LEAN_INFO(TAG, "Feishu WS connected");
      break;

    case WEBSOCKET_EVENT_DISCONNECTED:
      ctx->ws_connected = false;
      LEAN_WARN(TAG, "Feishu WS disconnected");
      break;

    case WEBSOCKET_EVENT_DATA:
      if (e->op_code == WS_TRANSPORT_OPCODES_BINARY) {
        if (e->payload_offset == 0) {
          lean_utils_dynamic_buffer_free(&ctx->ws_buffer);
          lean_utils_dynamic_buffer_init(&ctx->ws_buffer, e->payload_len);
        }

        if (!lean_utils_dynamic_buffer_get_data(&ctx->ws_buffer)) {
          LEAN_ERROR(TAG, "Failed to init ws_buffer");
          break;
        }

        size_t current_len = lean_utils_dynamic_buffer_get_length(&ctx->ws_buffer);
        size_t need        = current_len + e->data_len;
        size_t cap         = lean_utils_dynamic_buffer_get_capacity(&ctx->ws_buffer);
        if (need > cap) {
          LEAN_ERROR(TAG, "ws_buffer capacity exceeded");
          lean_utils_dynamic_buffer_free(&ctx->ws_buffer);
          break;
        }

        lean_utils_dynamic_buffer_append(&ctx->ws_buffer, e->data_ptr, e->data_len);
        const uint8_t* buf  = lean_utils_dynamic_buffer_get_data(&ctx->ws_buffer);
        size_t         size = lean_utils_dynamic_buffer_get_length(&ctx->ws_buffer);
        if (size >= e->payload_len) {
          feishu_ws_handle_message(ctx, buf, size);
          lean_utils_dynamic_buffer_free(&ctx->ws_buffer);
        }
      }
      break;

    default:
      break;
  }
}
static void feishu_thread(void* argv) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)argv;
  while (true) {
    if (!http_post_ws_config(ctx)) {
      LEAN_WARN(TAG, "WS config failed, retry in 5s");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (!ctx->ws_url) {
      LEAN_ERROR(TAG, "WS URL not available");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    esp_websocket_client_config_t ws_cfg = {
      .uri                    = ctx->ws_url,
      .buffer_size            = 2048,
      .task_stack             = FEISHU_WS_THREAD_SIZE,
      .reconnect_timeout_ms   = ctx->ws_reconnect_interval_ms,
      .network_timeout_ms     = 10000,
      .disable_auto_reconnect = false,
      .crt_bundle_attach      = esp_crt_bundle_attach,
    };

    ctx->ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ctx->ws_client) {
      LEAN_ERROR(TAG, "WS client init failed");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    esp_websocket_register_events(ctx->ws_client, WEBSOCKET_EVENT_ANY, feishu_ws_event_handler, ctx);
    esp_websocket_client_start(ctx->ws_client);

    if (!ctx->ws_connected) {
      vTaskDelay(pdMS_TO_TICKS(5000));
    }

    int64_t last_ping = 0;
    while (ctx->ws_client) {
      if (ctx->ws_connected) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - last_ping >= ctx->ws_ping_interval_ms) {
          memset(&ctx->ws_ping, 0, sizeof(ws_frame_t));
          ctx->ws_ping.seq_id       = 0;
          ctx->ws_ping.log_id       = 0;
          ctx->ws_ping.service      = ctx->ws_service_id;
          ctx->ws_ping.method       = 0;
          ctx->ws_ping.header_count = 1;
          strncpy(ctx->ws_ping.headers[0].key, "type", sizeof(ctx->ws_ping.headers[0].key) - 1);
          strncpy(ctx->ws_ping.headers[0].value, "ping", sizeof(ctx->ws_ping.headers[0].value) - 1);
          ws_send_frame(ctx, &ctx->ws_ping, NULL, 0, 1000);
          last_ping = now;
        }
      }

      if (!esp_websocket_client_is_connected(ctx->ws_client) && !ctx->ws_connected) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    esp_websocket_client_stop(ctx->ws_client);
    esp_websocket_client_destroy(ctx->ws_client);
    ctx->ws_client    = NULL;
    ctx->ws_connected = false;
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

lean_feishu_bot_handle_t lean_feishu_bot_create(lean_agent_handle agent, const char* app_id, const char* app_secret) {
  if (!app_id || !app_secret || !agent) {
    return NULL;
  }

  if (app_id[0] == '\0' || app_secret[0] == '\0') {
    LEAN_ERROR(TAG, "feishu credentials empty");
    return NULL;
  }

  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)calloc(1, sizeof(lean_feishu_bot_ctx_t));
  if (NULL == ctx) {
    LEAN_ERROR(TAG, "feishu malloc failed");
    return NULL;
  }

  strncpy(ctx->app_id, app_id, sizeof(ctx->app_id) - 1);
  strncpy(ctx->app_secret, app_secret, sizeof(ctx->app_secret) - 1);
  ctx->token_expire_time = 0;
  ctx->agent_core        = agent;

  /* Initialize session cache */
  ctx->session_cache      = (session_item_t*)calloc(FEISHU_SESSION_CACHE_SIZE, sizeof(session_item_t));
  ctx->session_cache_size = FEISHU_SESSION_CACHE_SIZE;
  ctx->session_idx        = 0;
  if (!ctx->session_cache) {
    goto failed;
  }

  /* Initialize dedup cache */
  ctx->seen_msg_keys      = (uint64_t*)calloc(FEISHU_DEDUP_CACHE_SIZE, sizeof(uint64_t));
  ctx->seen_msg_keys_size = FEISHU_DEDUP_CACHE_SIZE;
  ctx->seen_msg_idx       = 0;
  if (!ctx->seen_msg_keys) {
    goto failed;
  }

  /* Initialize WS config */
  ctx->ws_ping_interval_ms      = 120000;
  ctx->ws_reconnect_interval_ms = 30000;
  ctx->ws_reconnect_nonce_ms    = 30000;
  lean_utils_dynamic_buffer_init(&ctx->ws_buffer, 0);
  return ctx;

failed:
  if (ctx->session_cache) {
    free(ctx->session_cache);
  }

  if (ctx->seen_msg_keys) {
    free(ctx->seen_msg_keys);
  }

  free(ctx);
  return NULL;
}

void lean_feishu_bot_destroy(lean_feishu_bot_handle_t* bot) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)*bot;
  if (!ctx) {
    return;
  }

  if (ctx->ws_task) {
    vTaskDelete(ctx->ws_task);
    ctx->ws_task = NULL;
  }

  if (ctx->ws_client) {
    esp_websocket_client_stop(ctx->ws_client);
    esp_websocket_client_destroy(ctx->ws_client);
    ctx->ws_client = NULL;
  }

  free(ctx->ws_url);
  free(ctx->session_cache);
  free(ctx->seen_msg_keys);

  ctx->ws_connected = false;
  *bot              = NULL;
}

bool lean_feishu_bot_start(lean_feishu_bot_handle_t bot) {
  lean_feishu_bot_ctx_t* ctx = (lean_feishu_bot_ctx_t*)bot;
  if (!ctx) {
    return false;
  }

  if (ctx->ws_task) {
    LEAN_WARN(TAG, "bot already started");
    return false;
  }

  if (ctx->app_id[0] == '\0' || ctx->app_secret[0] == '\0') {
    LEAN_WARN(TAG, "Feishu not configured, skipping WebSocket start");
    return true;
  }

  BaseType_t res = xTaskCreatePinnedToCore(feishu_thread,
                                           "feishu_bot",
                                           FEISHU_THREAD_STACK_SIZE,
                                           ctx,
                                           FEISHU_THREAD_PRIORITY,
                                           &ctx->ws_task,
                                           FEISHU_THREAD_PINNED_TO_CORE);

  if (res != pdPASS) {
    LEAN_ERROR(TAG, "bot create task failed");
    return false;
  }
  LEAN_INFO(TAG, "Feishu Bot started");
  return true;
}