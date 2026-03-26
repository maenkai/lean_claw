
#include "skill.h"
#define CONFIG_TOOL_CID_BASE (1000) // 避免和用户定义的id冲突,ID的起始为该id

typedef enum {
  TOOL_CID_BASE = CONFIG_TOOL_CID_BASE,
  TOOL_CID_SYSTEM_SLEEP,
  TOOL_CID_THREAD_READ,
  TOOL_CID_GPIO_INIT,
  TOOL_CID_GPIO_SET,
  TOOL_CID_GPIO_GET,
  TOOL_CID_PWM_INIT,
  TOOL_CID_PWM_SET
} tool_collection_id;

// 工具skill + exec 合集
static const lean_skill_config s_tool_skill[] = {
  { TOOL_CID_SYSTEM_SLEEP, "msleep", skill_param("i#ms"), "void" },
  { TOOL_CID_GPIO_INIT, "gpio init", skill_param("i#0=input 1=output", "i#gpio number", "i#down_en", "i#up_en"), "void" },
  { TOOL_CID_GPIO_SET, "gpio set,using after gpio init", skill_param("i#gpio number", "i# 0=low level 1=high level"), "void" },
  { TOOL_CID_GPIO_GET, "gpio get,using after gpio init", skill_param("i#gpio number"), "gpio level state" },
  { TOOL_CID_PWM_INIT, "pwm fade init", skill_param("i#duty_resolution", "i#freq_hz", "i#1=low_speed_mode 0=high_speed_mode", "i#timer_num", "i#channel", "i#gpio_num"), "void" },
  { TOOL_CID_PWM_SET, "pwm fade set, using after pwm init", skill_param("i#speed_mode", "i#channel", "i#duty", "i#fade_time"), "void" }
};

bool tool_gpio_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data);
bool tool_pwm_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data);
bool tool_thread_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data);

static const lean_skill_exec_cb s_tool_exec[] = {
  tool_gpio_exec,
  tool_pwm_exec,
  tool_thread_exec
};