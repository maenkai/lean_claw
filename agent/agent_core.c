#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "stdlib.h"
#include "string.h"

#include "utils_log.h"
#include "utils_list.h"
#include "llm_access.h"
#include "agent_core.h"
#include "executor.h"
#include "exec_thread.h"
#include "exec_timer.h"
#include "exec_scheduler.h"

#define TAG                          "agent"
#define AGENT_CORE_QUEUE_USER_SIZE   20
#define AGENT_CORE_QUEUE_AGENT_SIZE  10
#define AGENT_CORE_THREAD_STACK_SIZE (4 * 1024)
#define AGENT_CORE_THREAD_PRIORITY   (10 | portPRIVILEGE_BIT)
#define AGENT_CORE_EXEC_TIMEOUT      (30 * 1000)

typedef struct {
  bool                  sub_all;
  char                  name[16];
  lean_agent_llm_rsp_cb user_cb;
  void*                 user_data;
} _lean_agent_channel;

typedef struct {
  lean_agent_state           state;
  lean_agent_config          config;
  QueueHandle_t              queue_user;  // 用户接收消息对列
  QueueHandle_t              queue_agent; // agnet的消息对列
  lean_thread_node           thread;
  lean_scheduler_node        sche;
  lean_utils_list_node       channel_list;
  const _lean_agent_channel* channel_user;
  void*                      priv_data;
  uint64_t                   busy_last_time_sec;
} _lean_agent_handle;

typedef enum {
  AGENT_QUEUE_TYPE_INIT,
  AGENT_QUEUE_TYPE_DEINIT,
  AGENT_QUEUE_TYPE_ADD_CHANNEL,
  AGENT_QUEUE_TYPE_DEL_CHANNEL,
  AGENT_QUEUE_TYPE_USER_TO_LLM,
  AGENT_QUEUE_TYPE_LLM_TO_AGENT,
} agent_queue_type;

typedef struct {
  agent_queue_type type;
  union {
    struct {
      _lean_agent_channel* channel;
    } add_channel;

    struct {
      _lean_agent_channel* channel;
    } del_channel;

    struct {
      cJSON** content;
      int     counts;
    } llm2agent;

    struct {
      _lean_agent_channel* channel;
      char*                msg_ptr;
      int                  msg_len;
    } user2llm;
  } argv;
} agent_core_queue;

static const _lean_agent_channel s_local_channel = { .name = "Agent" };

/**
 * @brief 获取声明的提示词
 *
 * @return const char*
 */
static const char* agent_prompter_reference_get(void) {

  const char* prompter = "**你是LeanClaw**,ESP-IDF专家。严格按以下JSON格式交互:\n"
                         "\n"
                         "{\"msg\":\"回复用户\"}\n"
                         "\n"
                         "**核心规则:**\n"
                         "- 执行动作交互格式为:"
                         "\n"
                         "{\"msg\":\"(执行中)回复用户\",\"func\":[],\"thread\":{},\"sche\":{}, \"timer\":{}}\n"
                         "\n"
                         "- 根据技能表填充func数组\n"
                         "- 持续任务用thread,日期任务用sche,定式任务用timer\n"
                         "- 你需要理解用户的需求执行指令,不允许执行不存在的指令"
                         "\n"
                         "**func格式:**\n"
                         "[{\"id\":>=1, \"seq\":N, \"param\":[值]}]\n"
                         "- seq从1开始,每次调用递增\n"
                         "- param按技能表顺序传值(忽略#及前面内容)\n"
                         "\n"
                         "**thread参数:**\n"
                         "{\"name\":\"英文描述名\", \"stack\":≥2048, \"priority\":≥0, \"loop\":true/false, \"msleep\":毫秒, \"func\":[线程执行的指令]}\n"
                         "\n"
                         "**sche参数:**\n"
                         "{\"name\":\"英文描述名\", \"wday\":位掩码(周日=1,当天=127), \"seconds\":0~86399秒(执行日的触发秒数), \"once\":true/false, \"func\":[定时执行的指令]}\n"
                         "\n"
                         "**timer参数:**\n"
                         "{\"name\":\"英文描述名\", \"delay\":倒计时秒数, \"func\":[定时执行的指令]}\n"
                         "\n"
                         "**交互流程:**\n"
                         "你发指令 → 我返回 `{\"res\":[{\"id\":编号,\"seq\":序号,\"success\":true/false,\"val\":[返回值]}], \"thd_res\":线程创建结果, \"sche_res\":任务创建结果}` → 你解析res决定下一步\n"
                         "\n"
                         "**关键约束:**\n"
                         "1. seq全局递增\n"
                         "2. 有参数依赖的指令分开发:等待获取到我回复后的参数后再去发下一条指令\n"
                         "3. 复杂任务可拆分为多个func\n"
                         "4. 每次理解对话时要回来熟悉本规则\n"
                         "\n"
                         "**技能表:**\n"
                         "`{\"id\":>=1,\"desc\":\"功能描述,你需要熟读再去调用\",\"param\":[i#整形,b#布尔,s#字符串,f#浮点值],\"ret\":\"返回值\"}`";
  return prompter;
}

/**
 * @brief 发送消息对列
 *
 * @param hd
 * @param msg
 */
static bool agent_send_queue(_lean_agent_handle* hd, agent_core_queue* queue_cmd, bool agent) {
  QueueHandle_t queue = agent ? hd->queue_agent : hd->queue_user;
  if (NULL == queue || NULL == queue_cmd) {
    return false;
  }

  if (xQueueSend(queue, queue_cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
    LEAN_ERROR(TAG, "Send %s Queue failed!", agent ? "Agent" : "User");
    return false;
  }
  return true;
}

/**
 * @brief 来自大模型的回复
 *
 * @param err_code
 * @param message
 * @param counts
 * @param priv_data
 */
static void on_llm_response(lean_llm_error_code err_code, lean_llm_access_message* message, int counts, void* priv_data) {
  if (err_code != LLM_ERROR_NONE) {
    return;
  }

  _lean_agent_handle* hd           = (_lean_agent_handle*)priv_data;
  agent_core_queue    queue_cmd    = { .type = AGENT_QUEUE_TYPE_LLM_TO_AGENT };
  queue_cmd.argv.llm2agent.content = malloc(sizeof(cJSON*) * counts);
  queue_cmd.argv.llm2agent.counts  = 0;

  for (int i = 0; i < counts; i++) {
    if (message[i].role == LLM_ACCESS_ROLE_USER) {
      continue;
    }

    LEAN_INFO(TAG, "[LLM -> Agent] %s", message[i].content);
    cJSON* object = cJSON_Parse(message[i].content);
    if (NULL != object) {
      queue_cmd.argv.llm2agent.content[queue_cmd.argv.llm2agent.counts++] = object;
    }
  }

  if (queue_cmd.argv.llm2agent.counts) {
    agent_send_queue(hd, &queue_cmd, true);
  } else {
    LEAN_ERROR(TAG, "invalid response");
    free(queue_cmd.argv.llm2agent.content);
  }
}

/**
 * @brief 大模型处理初始化
 *
 * @param hd
 */
static void on_agent_init(_lean_agent_handle* hd) {
  const char* tip              = "#技能表\r\n";
  const char* system_prompter  = agent_prompter_reference_get();
  char*       skill_prompter   = lean_skill_get_jsonstring(hd->config.skill);
  int         system_len       = strlen(system_prompter);
  int         tip_len          = strlen(tip);
  int         skill_len        = strlen(skill_prompter);
  int         prompter_length  = system_len + tip_len + skill_len + 1;
  int         prompter_offsect = 0;

  char* prompter = malloc(prompter_length);
  memcpy(&prompter[prompter_offsect], system_prompter, system_len);
  prompter_offsect += system_len;
  memcpy(&prompter[prompter_offsect], tip, tip_len);
  prompter_offsect += tip_len;
  memcpy(&prompter[prompter_offsect], skill_prompter, skill_len);
  prompter_offsect += skill_len;
  prompter[prompter_offsect] = '\0';
  cJSON_free(skill_prompter);

  lean_llm_access_message message = { 0 };
  message.role                    = LLM_ACCESS_ROLE_USER;
  message.content                 = prompter;
  hd->channel_user                = &s_local_channel;
  lean_llm_access_set_rsp_cb_register(hd->config.llm, on_llm_response);
  lean_llm_access_send_message(hd->config.llm, &message, 1, hd);
  free(prompter);
  LEAN_INFO(TAG, "agent init done");
}
/**
 * @brief 转发大模型的数据给到各个通道
 *
 * @param hd
 * @param message
 */
static void agent_message_notify_channel(_lean_agent_handle* hd, const char* message) {
  if (hd->channel_user == &s_local_channel) {
    return;
  }

  LEAN_WARN(TAG, "[LLM -> User(%s)] %s", hd->channel_user->name, message);

  lean_utils_list_foreach(hd->channel_list, channel_item, {
    _lean_agent_channel* channel = lean_utils_list_item_data_get(channel_item);
    if (channel != hd->channel_user && !channel->sub_all) {
      continue;
    }

    if (!channel->user_cb) {
      continue;
    }

    channel->user_cb(hd, (const lean_agent_channel)hd->channel_user, message, channel->user_data);
  })
}

/**
 * @brief 根据大模型的回复调用指令
 *
 * @param hd
 * @param message
 */
static void on_agent_peform_exec(_lean_agent_handle* hd, agent_core_queue* queue_cmd) {
  bool   dialogue = true;
  cJSON* root     = cJSON_CreateObject();
  cJSON* func_res = cJSON_CreateArray();

  for (int i = 0; i < queue_cmd->argv.llm2agent.counts; i++) {
    cJSON* msg_item    = cJSON_GetObjectItem(queue_cmd->argv.llm2agent.content[i], "msg");
    cJSON* func_item   = cJSON_GetObjectItem(queue_cmd->argv.llm2agent.content[i], "func");
    cJSON* thread_item = cJSON_GetObjectItem(queue_cmd->argv.llm2agent.content[i], "thread");
    cJSON* sche_item   = cJSON_GetObjectItem(queue_cmd->argv.llm2agent.content[i], "sche");
    cJSON* timer_item  = cJSON_GetObjectItem(queue_cmd->argv.llm2agent.content[i], "timer");

    if (func_item != NULL && cJSON_GetArraySize(func_item)) {
      dialogue = false;
      lean_exec_function_call(hd->config.exec, func_item, func_res);
    }

    if (thread_item != NULL && cJSON_GetArraySize(thread_item)) {
      dialogue = false;
      lean_thread_handle_json_cmd(hd->thread, thread_item, root);
    }

    if (sche_item != NULL && cJSON_GetArraySize(sche_item)) {
      dialogue = false;
      lean_scheduler_handle_json_cmd(hd->sche, sche_item, root);
    }

    if (timer_item != NULL && cJSON_GetArraySize(timer_item)) {
      dialogue = false;
      lean_agent_timer_handle_json_cmd(hd->config.exec, timer_item, root);
    }

    if (dialogue) {
      hd->state = AGENT_CORE_STATE_ON_IDLE;
    }

    if (NULL != msg_item) {
      agent_message_notify_channel(hd, msg_item->valuestring);
    }

    cJSON_Delete(queue_cmd->argv.llm2agent.content[i]);
  }

  if (dialogue) {
    cJSON_Delete(func_res);
    cJSON_Delete(root);
    return;
  }

  hd->state              = AGENT_CORE_STATE_ON_BUSY;
  hd->busy_last_time_sec = esp_timer_get_time() / 1000000;

  if (cJSON_GetArraySize(func_res)) {
    cJSON_AddItemToObject(root, "res", func_res);
  }

  lean_llm_access_message user_msg = { 0 };
  user_msg.role                    = LLM_ACCESS_ROLE_USER;
  user_msg.content                 = cJSON_PrintUnformatted(root);
  LEAN_INFO(TAG, "[Agent -> LLM] %s", user_msg.content);
  lean_llm_access_send_message(hd->config.llm, &user_msg, 1, hd);
  cJSON_free((void*)user_msg.content);
  cJSON_Delete(root);
}

/**
 * @brief 将用户信息转发给大模型
 *
 * @param hd
 * @param queue_cmd
 */
static void on_agent_send_user_message(_lean_agent_handle* hd, agent_core_queue* queue_cmd) {
  hd->state                        = AGENT_CORE_STATE_ON_BUSY;
  hd->busy_last_time_sec           = esp_timer_get_time() / 1000000;
  lean_llm_access_message user_msg = { 0 };
  user_msg.role                    = LLM_ACCESS_ROLE_USER;
  user_msg.content                 = queue_cmd->argv.user2llm.msg_ptr;
  hd->channel_user                 = queue_cmd->argv.user2llm.channel;
  LEAN_WARN(TAG, "[User(%s) -> Agent] %s", hd->channel_user->name, user_msg.content);
  lean_llm_access_send_message(hd->config.llm, &user_msg, 1, hd);
  free(queue_cmd->argv.user2llm.msg_ptr);
}

/**
 * @brief 智能体的线程
 *
 * @param argv
 */
static void agent_thread(void* argv) {
  _lean_agent_handle* hd = (_lean_agent_handle*)argv;
  agent_core_queue    queue_cmd;
  bool                running = true;

  while (running) {
    bool has_recv = false;

    do {
      if (xQueueReceive(hd->queue_agent, &queue_cmd, 0) == pdTRUE) {
        has_recv = true;
        break;
      }

      if (hd->state == AGENT_CORE_STATE_ON_BUSY) {
        if (esp_timer_get_time() / 100000 - hd->busy_last_time_sec >= AGENT_CORE_EXEC_TIMEOUT) {
          hd->state = AGENT_CORE_STATE_ON_IDLE;
          LEAN_WARN(TAG, "Ageent exec timeout");
        } else {
          break;
        }
      }

      if (xQueueReceive(hd->queue_user, &queue_cmd, 0) == pdTRUE) {
        has_recv = true;
        break;
      }

    } while (false);

    if (!has_recv) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    switch (queue_cmd.type) {
      case AGENT_QUEUE_TYPE_INIT:
        on_agent_init(hd);
        break;

      case AGENT_QUEUE_TYPE_DEINIT:
        running = false;
        break;

      case AGENT_QUEUE_TYPE_ADD_CHANNEL:
        if (!lean_utils_list_item_append(hd->channel_list, true, queue_cmd.argv.add_channel.channel)) {
          free(queue_cmd.argv.add_channel.channel);
        }
        break;

      case AGENT_QUEUE_TYPE_DEL_CHANNEL:
        lean_utils_list_item_remove(hd->channel_list, queue_cmd.argv.del_channel.channel);
        break;

      case AGENT_QUEUE_TYPE_USER_TO_LLM:
        on_agent_send_user_message(hd, &queue_cmd);
        break;

      case AGENT_QUEUE_TYPE_LLM_TO_AGENT:
        on_agent_peform_exec(hd, &queue_cmd);
        break;
    }
  }

  vQueueDelete(hd->queue_user);
  vQueueDelete(hd->queue_agent);
  hd->queue_user  = NULL;
  hd->queue_agent = NULL;
  vTaskDelete(NULL);
}

/**
 * @brief 创建一个智能体
 *
 * @param config
 * @return lean_agent_handle
 */
lean_agent_handle lean_agent_create(lean_agent_config* config) {
  _lean_agent_handle* hd = calloc(1, sizeof(_lean_agent_handle));
  hd->config             = *config;
  hd->queue_user         = xQueueCreate(AGENT_CORE_QUEUE_USER_SIZE, sizeof(agent_core_queue));
  hd->queue_agent        = xQueueCreate(AGENT_CORE_QUEUE_AGENT_SIZE, sizeof(agent_core_queue));
  hd->thread             = lean_thread_node_create(hd->config.exec);
  hd->sche               = lean_scheduler_create_node(hd->config.exec, NULL);
  hd->channel_list       = lean_utils_list_node_create(false);

  lean_exec_ctx ctx = { .agent = hd };
  lean_exec_set_ctx(hd->config.exec, &ctx);

  if (hd->queue_user == NULL) {
    LEAN_ERROR(TAG, "Create User Queue failed!");
    goto fail;
  }

  if (hd->queue_agent == NULL) {
    LEAN_ERROR(TAG, "Create Agent Queue failed!");
    goto fail;
  }

  if (xTaskCreate(agent_thread, "agent_thread", AGENT_CORE_THREAD_STACK_SIZE, hd,
                  AGENT_CORE_THREAD_PRIORITY, NULL)
      != pdTRUE) {
    LEAN_ERROR(TAG, "Create Thread failed!");
    goto fail;
  }

  agent_core_queue queue_cmd = { 0 };
  queue_cmd.type             = AGENT_QUEUE_TYPE_INIT;
  agent_send_queue(hd, &queue_cmd, true);
  return hd;

fail:
  if (NULL != hd->queue_user) {
    vQueueDelete(hd->queue_user);
  }
  if (NULL != hd->queue_agent) {
    vQueueDelete(hd->queue_agent);
  }
  free(hd);
  return NULL;
}

/**
 * @brief 向智能体发送信息
 *
 * @param hd
 * @param msg
 */
void lean_agent_send_message(lean_agent_handle hd, lean_agent_channel channel, const char* msg) {
  _lean_agent_handle* core_hd = (_lean_agent_handle*)hd;
  if (core_hd->queue_user == NULL || core_hd->queue_agent == NULL || channel == NULL) {
    return;
  }

  agent_core_queue queue_cmd      = { .type = AGENT_QUEUE_TYPE_USER_TO_LLM };
  queue_cmd.argv.user2llm.channel = channel;
  queue_cmd.argv.user2llm.msg_len = strlen(msg);
  queue_cmd.argv.user2llm.msg_ptr = malloc(queue_cmd.argv.user2llm.msg_len + 1);
  memcpy(queue_cmd.argv.user2llm.msg_ptr, msg, queue_cmd.argv.user2llm.msg_len);
  queue_cmd.argv.user2llm.msg_ptr[queue_cmd.argv.user2llm.msg_len] = '\0';
  agent_send_queue(hd, &queue_cmd, false);
}

/**
 * @brief 获取智能体的线程节点
 *
 * @param hd
 * @return lean_thread_node
 */
lean_thread_node lean_agent_get_thread_node(lean_agent_handle hd) {
  _lean_agent_handle* core_hd = (_lean_agent_handle*)hd;
  return core_hd->thread;
}

/**
 * @brief 订阅智能体返回消息
 *
 * @param hd 智能体
 * @param name 名称
 * @param cb 接收回调
 * @param sub_all_channel = true时,非回复本通道的消息也能接收到
 * @return lean_agent_channel
 */
lean_agent_channel lean_agent_channel_create(lean_agent_handle hd, const char* name, lean_agent_llm_rsp_cb user_cb, void* user_data,
                                             bool sub_all_channel) {
  _lean_agent_handle* core_hd = (_lean_agent_handle*)hd;
  if (core_hd->queue_user == NULL || core_hd->queue_agent == NULL) {
    return NULL;
  }

  _lean_agent_channel* channel = calloc(1, sizeof(_lean_agent_channel));
  if (NULL == channel) {
    return NULL;
  }

  strncpy(channel->name, name, 16);
  channel->sub_all   = sub_all_channel;
  channel->user_cb   = user_cb;
  channel->user_data = user_data;

  agent_core_queue queue_cmd = { .type = AGENT_QUEUE_TYPE_ADD_CHANNEL, .argv.add_channel.channel = channel };
  if (!agent_send_queue(hd, &queue_cmd, true)) {
    free(channel);
    channel = NULL;
  }
  return channel;
}

/**
 * @brief 删除消息通道
 *
 * @param ch
 */
void lean_agent_channel_delete(lean_agent_handle hd, lean_agent_channel* ch) {
  if (ch == NULL || *ch == NULL) {
    return;
  }
  agent_core_queue queue_cmd = { .type = AGENT_QUEUE_TYPE_DEL_CHANNEL, .argv.del_channel.channel = *ch };
  if (!agent_send_queue(hd, &queue_cmd, true)) {
    return;
  }
  *ch = NULL;
}

/**
 * @brief 获取智能体当前的状态
 *
 * @param hd
 * @return lean_agent_state
 */
lean_agent_state lean_agent_state_get(lean_agent_handle hd) {
  return ((_lean_agent_handle*)hd)->state;
}