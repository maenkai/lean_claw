#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "string.h"

#include "time.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "utils_log.h"
#include "exec_scheduler.h"
#include "executor.h"

#define TAG                    "lean_scheduler_"
#define SCHE_THIS_WDAY_MASK    0x80 // 0x80 表示当今天
#define SCHE_CHECK_INTERVAL_MS 1000 // 定时检测周期
#define SCHE_TRIGGER_ADVANCE_S 50   // 举例目标时间多少开始倒计时任务

typedef struct lean_scheduler_item {
  uint32_t                    id;
  uint8_t                     wdays;           // 星期几触发 (bitmask: 0x01=周一，0x7F=每天, 0x80=本日)
  cJSON*                      func;            // 执行的函数配置
  uint32_t                    seconds;         // 设定触发时间的秒数 (0~86399)
  uint64_t                    trigger_seconds; // 当天触发的绝对时间戳 (ms)
  bool                        once;            // 是否仅仅执行一次,执行后会删除
  struct lean_scheduler_item* next;
} lean_scheduler_item;

typedef struct {
  lean_scheduler_item*     head;
  esp_timer_handle_t       timer;
  lean_exec_handle         exec;
  SemaphoreHandle_t        mutex;
  uint32_t                 next_id;
  lean_scheduler_finish_cb cb;
} _lean_scheduler_node;

/**
 * @brief 获取当前时间（秒）
 */
static uint32_t get_current_seconds(void) {
  time_t    now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  return timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
}

/**
 * @brief 获取当前星期几
 */
static uint8_t get_current_weekday(void) {
  time_t    now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  return timeinfo.tm_wday;
}

/**
 * @brief 获取当前绝对时间戳 (ms)
 */
static uint64_t get_current_elapsed_seconds(void) {
  return esp_timer_get_time() / 1000000;
}

/**
 * @brief 添加定时项到链表
 */
static lean_scheduler_item* add_sche_item_to_list(_lean_scheduler_node* node, lean_scheduler_item* item) {
  if (xSemaphoreTake(node->mutex, portMAX_DELAY) != pdTRUE) {
    return NULL;
  }
  item->next = node->head;
  node->head = item;
  xSemaphoreGive(node->mutex);
  return item;
}

/**
 * @brief 从链表删除定时项
 */
static void remove_sche_item_from_list(_lean_scheduler_node* node, uint32_t id) {
  if (xSemaphoreTake(node->mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  lean_scheduler_item* current = node->head;
  lean_scheduler_item* prev    = NULL;

  while (current != NULL) {
    if (current->id == id) {
      if (prev == NULL) {
        node->head = current->next;
      } else {
        prev->next = current->next;
      }
      cJSON_Delete(current->func);
      free(current);
      break;
    }
    prev    = current;
    current = current->next;
  }

  xSemaphoreGive(node->mutex);
}

/**
 * @brief 检测到是否到运行时间
 *
 * @param node
 */
static void check_and_trigger_sche(_lean_scheduler_node* node) {
  if (xSemaphoreTake(node->mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  uint64_t elapsed_seconds = get_current_elapsed_seconds();
  uint32_t current_seconds = get_current_seconds();
  uint8_t  current_weekday = get_current_weekday();

  lean_scheduler_item* item = node->head;
  lean_scheduler_item* prev = NULL;
  lean_scheduler_item* next = NULL;

  while (item != NULL) {
    next = item->next; // 提前保存下一个节点，防止删除后访问野指针

    // 检查星期几是否匹配 (bitmask)
    uint8_t weekday_bit = 1 << current_weekday;
    if (!(item->wdays & weekday_bit) && item->wdays != SCHE_THIS_WDAY_MASK) {
      prev = item;
      item = next;
      continue;
    }

    // 检查是否接近触发时间（提前 50 秒填充 trigger_seconds）
    if (item->trigger_seconds == 0) {
      int32_t diff = item->seconds - current_seconds;
      if (diff <= SCHE_TRIGGER_ADVANCE_S && diff >= 0) {
        item->trigger_seconds = elapsed_seconds + diff;
        LEAN_INFO(TAG, "Timer %d prepare trigger at %d seconds", item->id, item->seconds);
      } else {
        prev = item;
        item = next;
        continue;
      }
    }

    // 检查是否到达触发时间
    if (item->trigger_seconds > 0 && elapsed_seconds >= item->trigger_seconds) {
      LEAN_INFO(TAG, "Timer %d triggered, executing function", item->id);

      cJSON* result = node->cb ? cJSON_CreateObject() : NULL;

      // 执行定时任务
      lean_exec_function_call(node->exec, item->func, result);

      // 调用完成回调
      if (node->cb) {
        node->cb(item->id, result);
        cJSON_Delete(result);
      }

      // 判断是否需要删除：once 为 true 或 仅今天执行
      if ((item->once || item->wdays == SCHE_THIS_WDAY_MASK)) {
        LEAN_INFO(TAG, "Timer %d will be deleted (once=%d, wdays=0x%02x)", item->id, item->once, item->wdays);
        // 从链表删除
        if (prev == NULL) {
          node->head = next;
        } else {
          prev->next = next;
        }

        cJSON_Delete(item->func);
        free(item);

        // 删除后不更新 prev，因为 prev 保持不变
        item = next;
        continue;
      }
    }

    prev = item;
    item = next;
  }

  xSemaphoreGive(node->mutex);
}

/**
 * @brief 定时器回调函数
 */
static void timer_callback(void* arg) {
  _lean_scheduler_node* node = (_lean_scheduler_node*)arg;
  check_and_trigger_sche(node);
}

/**
 * @brief 创建定时器节点
 *
 * @param skill
 * @return lean_scheduler_node
 */
lean_scheduler_node lean_scheduler_create_node(lean_exec_handle exec, lean_scheduler_finish_cb finish_cb) {
  _lean_scheduler_node* node = (_lean_scheduler_node*)malloc(sizeof(_lean_scheduler_node));
  if (NULL == node) {
    return NULL;
  }

  memset(node, 0, sizeof(_lean_scheduler_node));
  node->cb    = finish_cb;
  node->exec  = exec;
  node->mutex = xSemaphoreCreateMutex();
  if (node->mutex == NULL) {
    free(node);
    return NULL;
  }

  const esp_timer_create_args_t timer_args = {
    .callback              = timer_callback,
    .arg                   = node,
    .dispatch_method       = ESP_TIMER_TASK,
    .name                  = "lean_scheduler_",
    .skip_unhandled_events = false,
  };

  esp_err_t err = esp_timer_create(&timer_args, &node->timer);
  if (err != ESP_OK) {
    vSemaphoreDelete(node->mutex);
    free(node);
    return NULL;
  }

  // 启动定时器，每秒检查一次
  err = esp_timer_start_periodic(node->timer, SCHE_CHECK_INTERVAL_MS * 1000);
  if (err != ESP_OK) {
    esp_timer_delete(node->timer);
    vSemaphoreDelete(node->mutex);
    free(node);
    return NULL;
  }

  node->next_id = 1;
  return node;
}

/**
 * @brief 添加定时任务
 */
uint32_t lean_scheduler_add(lean_scheduler_node node, bool once, uint8_t wdays, uint32_t seconds, cJSON* func) {
  _lean_scheduler_node* sche_node = (_lean_scheduler_node*)node;
  if (NULL == sche_node || NULL == func) {
    return 0;
  }

  lean_scheduler_item* item = (lean_scheduler_item*)malloc(sizeof(lean_scheduler_item));
  if (NULL == item) {
    return 0;
  }

  memset(item, 0, sizeof(lean_scheduler_item));
  item->id              = sche_node->next_id++;
  item->wdays           = wdays;
  item->func            = cJSON_Duplicate(func, true);
  item->trigger_seconds = 0;
  item->seconds         = seconds;

  if (add_sche_item_to_list(sche_node, item) == NULL) {
    cJSON_Delete(item->func);
    free(item);
    return 0;
  }

  LEAN_INFO(TAG, "Sche added: id=%d, wdays=0x%02x, time=%ds", item->id, wdays, seconds);
  return item->id;
}

/**
 * @brief 删除定时任务
 */
void lean_scheduler_delete(lean_scheduler_node node, uint32_t id) {
  _lean_scheduler_node* sche_node = (_lean_scheduler_node*)node;
  if (NULL == sche_node) {
    return;
  }

  remove_sche_item_from_list(sche_node, id);
  LEAN_INFO(TAG, "Sche deleted: id=%d", id);
}

/**
 * @brief 获取定时列表 JSON
 */
cJSON* lean_scheduler_get_list_to_json(lean_scheduler_node node) {
  _lean_scheduler_node* sche_node = (_lean_scheduler_node*)node;
  if (NULL == sche_node) {
    return NULL;
  }

  cJSON* result = cJSON_CreateArray();
  if (NULL == result) {
    return NULL;
  }

  if (xSemaphoreTake(sche_node->mutex, portMAX_DELAY) != pdTRUE) {
    cJSON_Delete(result);
    return NULL;
  }

  lean_scheduler_item* item = sche_node->head;
  while (item != NULL) {
    cJSON* timer_info = cJSON_CreateObject();
    if (timer_info) {
      cJSON_AddNumberToObject(timer_info, "id", item->id);
      cJSON_AddNumberToObject(timer_info, "wdays", item->wdays);
      cJSON_AddNumberToObject(timer_info, "seconds", item->seconds);
      cJSON_AddItemToArray(result, timer_info);
    }
    item = item->next;
  }

  xSemaphoreGive(sche_node->mutex);
  return result;
}

/**
 * @brief 销毁定时器节点
 */
void lean_scheduler_destroy(lean_scheduler_node node) {
  _lean_scheduler_node* sche_node = (_lean_scheduler_node*)node;
  if (NULL == sche_node) {
    return;
  }

  // 停止定时器
  if (sche_node->timer) {
    esp_timer_stop(sche_node->timer);
    esp_timer_delete(sche_node->timer);
  }

  // 释放所有定时项
  lean_scheduler_item* item = sche_node->head;
  while (item != NULL) {
    lean_scheduler_item* next = item->next;
    cJSON_Delete(item->func);
    free(item);
    item = next;
  }

  // 释放互斥锁和节点
  if (sche_node->mutex) {
    vSemaphoreDelete(sche_node->mutex);
  }
  free(sche_node);
}

/**
 * @brief 解析 timer JSON 命令并创建定时任务
 *
 * @param sche_node 定时器节点
 * @param timer_json timer 配置 JSON 对象
 * @param skill 技能句柄
 * @param op_result 操作结果 JSON 对象
 */
void lean_scheduler_handle_json_cmd(lean_scheduler_node sche_node, cJSON* timer_item, cJSON* op_result) {
  cJSON*      sche_res   = cJSON_CreateObject();
  const char* err_string = NULL;

  // 解析 name
  const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(timer_item, "name"));
  if (name == NULL) {
    name = "unnamed_sche";
  }

  // 解析 wday (星期几位掩码)
  cJSON* wday_item = cJSON_GetObjectItem(timer_item, "wday");
  if (wday_item == NULL) {
    err_string = "timer wday missing";
    goto failed;
  }
  uint8_t wdays = (uint8_t)cJSON_GetNumberValue(wday_item);

  // 解析 seconds
  cJSON* daytime_item = cJSON_GetObjectItem(timer_item, "seconds");
  if (daytime_item == NULL) {
    err_string = "timer seconds missing";
    goto failed;
  }
  uint32_t seconds = (uint32_t)cJSON_GetNumberValue(daytime_item);
  if (seconds >= 86400) {
    err_string = "seconds out of range (0~86399)";
    goto failed;
  }

  // 解析 once 标志
  bool   once      = false;
  cJSON* once_item = cJSON_GetObjectItem(timer_item, "once");
  if (once_item != NULL) {
    once = cJSON_IsTrue(once_item);
  }

  // 解析 func 数组
  cJSON* func = cJSON_GetObjectItem(timer_item, "func");
  if (func == NULL || !cJSON_GetArraySize(func)) {
    err_string = "function empty";
    goto failed;
  }

  // 创建定时任务
  uint32_t timer_id = lean_scheduler_add(sche_node, once, wdays, seconds, func);
  if (timer_id == 0) {
    err_string = "create failed";
    goto failed;
  }

  // 添加结果到返回对象
  if (op_result) {
    cJSON_AddNumberToObject(sche_res, "id", timer_id);
    cJSON_AddStringToObject(sche_res, "name", name);
    cJSON_AddNumberToObject(sche_res, "wday", wdays);
    cJSON_AddNumberToObject(sche_res, "seconds", seconds);
    cJSON_AddBoolToObject(sche_res, "once", once);
    cJSON_AddItemToObject(op_result, "sche_res", sche_res);
  } else {
    cJSON_Delete(sche_res);
  }

  LEAN_INFO(TAG, "Sche created: name=%s, id=%d, wday=0x%02x, time=%ds, once=%d",
            name, timer_id, wdays, seconds, once);
  return;

failed:
  if (op_result && err_string) {
    cJSON_AddStringToObject(op_result, "sche_res", err_string);
  }
  if (sche_res) {
    cJSON_Delete(sche_res);
  }
  if (err_string) {
    LEAN_ERROR(TAG, "%s", err_string);
  }
}