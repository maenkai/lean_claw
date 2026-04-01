#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "utils_log.h"
#include "exec_thread.h"
#include "executor.h"

#define TAG                         "agent_thread"
#define JSON_KEY_THREAD_RES         "thd_res"
#define AGENT_THREAD_MIN_STACK_SIZE (1024 * 2)

typedef struct {
  bool             loop;
  int              msleep;
  lean_exec_handle exec;
  cJSON*           func;
} lean_thread_param;

typedef struct thread_list_item {
  uint32_t                 id;
  TaskHandle_t             hd;
  lean_thread_param*       param;
  struct thread_list_item* next;
} thread_list_item;

typedef struct {
  uint32_t          next_id;
  thread_list_item* head;
  SemaphoreHandle_t mutex;
  lean_exec_handle  exec;
} _lean_thread_node;

/**
 * @brief Create a thread node object
 *
 * @return lean_thread_node
 */
lean_thread_node lean_thread_node_create(lean_exec_handle exec) {
  _lean_thread_node* manager = malloc(sizeof(_lean_thread_node));
  if (NULL == manager) {
    return NULL;
  }
  manager->head    = NULL;
  manager->next_id = 1;
  manager->mutex   = xSemaphoreCreateRecursiveMutex();
  manager->exec    = exec;
  if (manager->mutex == NULL) {
    free(manager);
    return NULL;
  }
  return manager;
}

/**
 * @brief 添加线程到列表
 *
 * @param node
 * @param hd
 * @param para
 * @return int
 */
static uint32_t add_thread_to_list(_lean_thread_node* node, TaskHandle_t hd, lean_thread_param* param) {
  if (NULL == node || NULL == param) {
    return 0;
  }

  thread_list_item* new_node = malloc(sizeof(thread_list_item));
  if (NULL == new_node) {
    return 0;
  }

  node->next_id   = node->next_id + 1;
  node->next_id   = node->next_id ? node->next_id : 1;
  new_node->id    = node->next_id;
  new_node->param = param;
  new_node->hd    = hd;
  new_node->next  = NULL;

  if (xSemaphoreTakeRecursive(node->mutex, portMAX_DELAY) != pdTRUE) {
    free(new_node);
    return 0;
  }

  new_node->next = node->head;
  node->head     = new_node;

  xSemaphoreGiveRecursive(node->mutex);
  return new_node->id;
}

/**
 * @brief 线程处理
 *
 * @param argv
 */
static void exec_thread(void* argv) {
  lean_thread_param* param = (lean_thread_param*)argv;
  while (param->loop) {
    lean_exec_function_call(param->exec, param->func, NULL);
    vTaskDelay(param->msleep / portTICK_PERIOD_MS);
  }

  cJSON_Delete(param->func);
  free(param);
  vTaskDelete(NULL);
}

/**
 * @brief 添加线程，执行func,返回任务id
 *
 * @param node
 * @param name
 * @param loop
 * @param stack_size
 * @param msleep
 * @param func
 * @return int
 */
int lean_thread_add(lean_thread_node node, const char* name, bool loop, uint32_t stack_size,
                    uint32_t priorty, uint32_t msleep, cJSON* func) {
  lean_thread_param* thread_param = malloc(sizeof(lean_thread_param));
  if (NULL == thread_param) {
    ESP_LOGE(TAG, "memory not enough");
    return 0;
  }

  TaskHandle_t task_handle = NULL;
  thread_param->loop       = loop;
  thread_param->msleep     = msleep;
  thread_param->func       = cJSON_Duplicate(func, true);
  thread_param->exec       = ((_lean_thread_node*)node)->exec;

  if (stack_size <= AGENT_THREAD_MIN_STACK_SIZE) {
    stack_size = AGENT_THREAD_MIN_STACK_SIZE;
  }

  if (priorty >= 25) {
    priorty = 24;
  }

  int ret = xTaskCreate(exec_thread, name, stack_size, thread_param, priorty, &task_handle);

  if (ret != pdTRUE) {
    free(thread_param);
    ESP_LOGE(TAG, "thread create task failed");
    return 0;
  }

  if (!thread_param->loop) {
    return 0;
  }

  return add_thread_to_list(node, task_handle, thread_param);
}

/**
 * @brief 创建线程并且执行对应的function
 *
 * @param thrrad
 * @param skill
 * @param op_result
 */
void lean_thread_handle_json_cmd(lean_thread_node node, cJSON* thread, cJSON* op_result) {
  const char* err_string = NULL;

  cJSON* func = cJSON_GetObjectItem(thread, "func");
  if (func == NULL || !cJSON_GetArraySize(func)) {
    err_string = "failed! thread func empty";
    goto failed;
  }

  const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(thread, "name"));
  if (!name) {
    err_string = "failed! thread name empty";
    goto failed;
  }

  int      stack_size = cJSON_GetNumberValue(cJSON_GetObjectItem(thread, "stack"));
  int      priorty    = cJSON_GetNumberValue(cJSON_GetObjectItem(thread, "priority"));
  bool     loop       = cJSON_GetNumberValue(cJSON_GetObjectItem(thread, "loop"));
  uint32_t msleep     = cJSON_GetNumberValue(cJSON_GetObjectItem(thread, "msleep"));

  if (lean_thread_add(node, name, loop, stack_size, priorty, msleep, func) == 0 && loop) {
    err_string = "thread create task failed";
    goto failed;
  }

  if (op_result) {
    cJSON_AddStringToObject(op_result, JSON_KEY_THREAD_RES, "success");
  }

  return;

failed:
  if (op_result && err_string) {
    cJSON_AddStringToObject(op_result, JSON_KEY_THREAD_RES, err_string);
  }

  if (err_string) {
    LEAN_ERROR(TAG, "%s", err_string);
  }
  return;
}

/**
 * @brief 通过id 获取删除线程
 *
 * @param node
 * @param id
 * @return int
 */
void lean_thread_delete(lean_thread_node thread_node, int id) {
  _lean_thread_node* node = (_lean_thread_node*)thread_node;
  if (NULL == node || NULL == node->mutex) {
    return;
  }

  if (xSemaphoreTakeRecursive(node->mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  thread_list_item* current = node->head;
  thread_list_item* prev    = NULL;

  while (current != NULL) {
    if (current->id == id) {
      if (prev == NULL) {
        node->head = current->next;
      } else {
        prev->next = current->next;
      }

      current->param->loop = false;
      free(current);
      break;
    }
    prev    = current;
    current = current->next;
  }

  xSemaphoreGiveRecursive(node->mutex);
}

/**
 * @brief 通过json获取线程列表
 *
 * @param node
 * @return cJSON*
 */
cJSON* lean_thread_get_list_to_json(lean_thread_node thread_node) {
  _lean_thread_node* node = (_lean_thread_node*)thread_node;
  if (NULL == node || NULL == node->mutex) {
    return NULL;
  }

  cJSON* result = cJSON_CreateArray();
  if (NULL == result) {
    return NULL;
  }

  if (xSemaphoreTakeRecursive(node->mutex, portMAX_DELAY) != pdTRUE) {
    cJSON_Delete(result);
    return NULL;
  }

  thread_list_item* current = node->head;
  while (current != NULL) {
    cJSON* thread_info = cJSON_CreateObject();
    if (thread_info) {
      cJSON_AddNumberToObject(thread_info, "id", current->id);
      cJSON_AddStringToObject(thread_info, "name", pcTaskGetName(current->hd));
      cJSON_AddItemToArray(result, thread_info);
    }
    current = current->next;
  }

  xSemaphoreGiveRecursive(node->mutex);
  return result;
}