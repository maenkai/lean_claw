#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "string.h"

#include "agent_config.h"
#include "console.h"
#include "cmd_wifi.h"
#include "cmd_chat.h"
#include "llm_access.h"
#include "agent_core.h"
#include "exec_thread.h"
#include "apt_qwen.h"
#include "apt_deepseek.h"
#include "utils_log.h"
#include "tool_collection.h"
#include "feishu_bot.h"

#define TAG "main_example"
static esp_timer_handle_t  s_network_timer_handle = NULL;
static lean_skill_handle  s_example_agent        = NULL;
static lean_feishu_bot_handle_t s_example_feishu_bot   = NULL;

enum example_skill_id {
  EXAMPLE_SKILL_PRINTF_TEST = 1,
  EXAMPLE_SKILL_THREAD_PS,
  EXAMPLE_SKILL_ID_MAX
};

static const lean_skill_config example_skill[] = {
  { .id = EXAMPLE_SKILL_PRINTF_TEST, .desc = "打印hello world", .param = skill_param(""), .ret = "void" },
  { .id = EXAMPLE_SKILL_THREAD_PS, .desc = "查看所有线程的信息", .param = skill_param(""), .ret = "thread list" }
};

/**
 * @brief skill函数执行处
 *
 * @param input
 * @param output
 * @param prov_data
 * @return true
 * @return false
 */
static bool on_example_skill_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data) {
#if CONFIG_ENABLE_TOOL_COLLECTION
  if (tool_collection_exec(input, output, prov_data)) {
    return true;
  }
#endif

  switch (input->id) {
    case EXAMPLE_SKILL_PRINTF_TEST: {
      printf("hello world\r\n");
      break;
    }

    case EXAMPLE_SKILL_THREAD_PS: {
      lean_thread_node node        = agent_core_get_thread_node(s_example_agent);
      cJSON*            object      = lean_thread_get_list_to_json(node);
      char*             json_string = cJSON_PrintUnformatted(object);
      lean_skill_result_value_string_append(output, json_string);
      cJSON_free(json_string);
      cJSON_Delete(object);
      break;
    }
  }

  lean_skill_result_set_success(output, true);
  return true;
}

/**
 * @brief 网络连接完成定时回调
 *
 * @param priv_data
 */
static void on_network_connected_timer(void* priv_data) {
  if (strlen(CONFIG_LLM_PRIVATE_API_KEY) == 0 || strlen(CONFIG_LLM_MODEL_NAME) == 0) {
    LEAN_WARN(TAG, "Agent not configured!");
  } else {
    lean_llm_access_config llm_config = {
      .api_key = CONFIG_LLM_PRIVATE_API_KEY,
      .model   = CONFIG_LLM_MODEL_NAME,
    };
    lean_agent_core_config agent_config = {
      agent_config.llm   = lean_llm_access_create(LLM_ACCESS_TYPE_DEEPSEEK, &llm_config),
      agent_config.skill = lean_skill_create(on_example_skill_exec, NULL)
    };
    lean_skill_append(agent_config.skill, example_skill, sizeof(example_skill) / sizeof(lean_skill_config));
    lean_skill_append(agent_config.skill, tool_collection_config_get(), tool_collection_counts_get());
    s_example_agent = lean_agent_core_create(&agent_config);
  }

  if (strlen(CONFIG_FEISHU_CRED_APPID) == 0 || strlen(CONFIG_FEISHU_CRED_APPSEC) == 0) {
    LEAN_WARN(TAG, "Feishu credentials not configured!");
  } else {
    s_example_feishu_bot = lean_feishu_bot_create(s_example_agent, CONFIG_FEISHU_CRED_APPID, CONFIG_FEISHU_CRED_APPSEC);
    lean_feishu_bot_start(s_example_feishu_bot);
  }

  if (s_network_timer_handle != NULL) {
    esp_timer_stop(s_network_timer_handle);
    esp_timer_delete(s_network_timer_handle);
    s_network_timer_handle = NULL;
  }
}

/**
 * @brief 控制台的内容输入
 *
 * @param data
 * @param length
 */
static void on_console_chat(const char* data, int length) {
  if (NULL == s_example_agent || data == NULL || length == 0) {
    LEAN_ERROR(TAG, "LLM access handle is not initialized");
    return;
  }

  static lean_agent_core_channel example_cli_channel = NULL;
  if (NULL == example_cli_channel) {
    example_cli_channel = lean_agent_core_channel_create(s_example_agent, "console", NULL, NULL, false);
  }

  agent_core_send_message(s_example_agent, example_cli_channel, data);
}

/**
 * @brief wifi event callback
 *
 * @param event
 * @param argv
 */
static void on_wifi_event(cmd_wifi_event event, void* argv) {
  if (CMD_WIFI_ON_GOTIP == event && NULL == s_example_agent && NULL == s_network_timer_handle) {
    esp_timer_create_args_t timer_args = {
      .callback        = on_network_connected_timer,
      .arg             = s_network_timer_handle,
      .dispatch_method = ESP_TIMER_TASK,
      .name            = "network_connected_timer",
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_network_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_once(s_network_timer_handle, 5000000));
  }
}

/**
 * @brief 业务入口
 *
 */
void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES
      || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  console_init();
  cmd_register_wifi(on_wifi_event);
  cmd_register_chat(on_console_chat);
}