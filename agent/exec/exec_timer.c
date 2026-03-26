#include "exec_timer.h"
#include "executor.h"
#include "utils_log.h"
#include "stdlib.h"
#include "string.h"
#include "esp_timer.h"

#define TAG              "agent_timer"
#define AGENT_RETURN_KEY "tm_res"

typedef struct {
  lean_timer_config  config;
  esp_timer_handle_t timer;
} _agent_timer_handle;

/**
 * @brief 定时器回调函数
 *
 * @param arg
 */
static void agent_timer_callback(void* arg) {
  _agent_timer_handle* hd = (_agent_timer_handle*)arg;

  LEAN_INFO(TAG, "Timer [%s] expired, executing func",
            hd->config.name ? hd->config.name : "noname");

  // 执行指令
  lean_executor_function_running(hd->config.func, hd->config.skill, NULL);

  // 清理资源
  free(hd->config.name);
  cJSON_Delete(hd->config.func);
  esp_timer_delete(hd->timer);
  free(hd);
}

/**
 * @brief 创建定时器
 */
bool agent_timer_create(lean_timer_config* config) {
  if (NULL == config || config->delay_sec <= 0) {
    LEAN_ERROR(TAG, "Invalid config");
    return false;
  }

  _agent_timer_handle* hd = malloc(sizeof(_agent_timer_handle));
  if (NULL == hd) {
    LEAN_ERROR(TAG, "Malloc failed");
    return false;
  }

  memset(hd, 0, sizeof(_agent_timer_handle));
  hd->config      = *config;
  hd->config.name = strdup(config->name);

  // 复制 func 数组（避免外部修改）
  if (config->func != NULL) {
    hd->config.func = cJSON_Duplicate(config->func, true);
  }

  // 创建 esp_timer
  esp_timer_create_args_t timer_args = {
    .callback              = agent_timer_callback,
    .arg                   = hd,
    .dispatch_method       = ESP_TIMER_TASK,
    .name                  = hd->config.name ? hd->config.name : "agent_timer",
    .skip_unhandled_events = false,
  };

  if (!hd->config.func || esp_timer_create(&timer_args, &hd->timer) != ESP_OK) {
    LEAN_ERROR(TAG, "Create esp_timer failed");
    if (hd->config.func) {
      cJSON_Delete(hd->config.func);
    }
    free(hd);
    return false;
  }

  // 启动一次性定时器
  uint64_t delay_us = config->delay_sec * 1000000LL;
  if (esp_timer_start_once(hd->timer, delay_us) != ESP_OK) {
    LEAN_ERROR(TAG, "Start timer failed");
    esp_timer_delete(hd->timer);
    if (hd->config.func) {
      cJSON_Delete(hd->config.func);
    }
    free(hd);
    return false;
  }

  LEAN_INFO(TAG, "Timer [%s] created, delay %d seconds",
            hd->config.name ? hd->config.name : "noname",
            config->delay_sec);
  return true;
}

/**
 * @brief 处理 JSON 命令创建定时器
 */
void agent_timer_handle_json_cmd(lean_skill_handle skill, cJSON* json_item, cJSON* root) {
  bool res = false;

  if (NULL == json_item) {
    return;
  }

  // 解析 timer 配置
  cJSON* name_item  = cJSON_GetObjectItem(json_item, "name");
  cJSON* delay_item = cJSON_GetObjectItem(json_item, "delay");
  cJSON* func_item  = cJSON_GetObjectItem(json_item, "func");

  if (NULL == delay_item || !cJSON_IsNumber(delay_item)) {
    LEAN_ERROR(TAG, "Invalid delay value");
    if (root) {
      cJSON* timer_res = cJSON_CreateObject();
      cJSON_AddBoolToObject(timer_res, "success", false);
      cJSON_AddStringToObject(timer_res, "error", "Delay must be > 0");
      cJSON_AddItemToObject(root, AGENT_RETURN_KEY, timer_res);
    }
    return;
  }

  int delay_sec = delay_item->valueint;
  if (delay_sec <= 0) {
    LEAN_ERROR(TAG, "Delay must be > 0");
    if (root) {
      cJSON* timer_res = cJSON_CreateObject();
      cJSON_AddBoolToObject(timer_res, "success", false);
      cJSON_AddStringToObject(timer_res, "error", "Delay must be > 0");
      cJSON_AddItemToObject(root, AGENT_RETURN_KEY, timer_res);
    }
    return;
  }

  lean_timer_config config = { 0 };
  config.name              = name_item ? name_item->valuestring : "timer";
  config.delay_sec         = delay_sec;
  config.func              = func_item;
  config.skill             = skill;

  // 创建定时器
  res = agent_timer_create(&config);

  // 返回结果
  if (root) {
    cJSON* timer_res = cJSON_CreateObject();
    cJSON_AddStringToObject(timer_res, "name", config.name);
    cJSON_AddNumberToObject(timer_res, "delay", config.delay_sec);
    cJSON_AddBoolToObject(timer_res, "success", res);
    cJSON_AddItemToObject(root, AGENT_RETURN_KEY, timer_res);
  }

  LEAN_INFO(TAG, "Timer command processed: %s, delay %ds, success=%d",
            config.name, config.delay_sec, res);
}