#include "tool_collection.h"
#include "tool_cid_def.h"

/**
 * @brief 获取配置
 *
 * @return lean_skill_config
 */
const lean_skill_config* tool_collection_config_get(void) {
  return s_tool_skill;
}

/**
 * @brief 配置数量获取
 *
 * @return int
 */
int tool_collection_counts_get(void) {
  return sizeof(s_tool_skill) / sizeof(lean_skill_config);
}

/**
 * @brief 工具运行合集
 *
 * @param input
 * @param output
 * @param prov_data
 * @return true
 * @return false
 */
bool tool_collection_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data) {
  bool hadnle = false;
  for (int i = 0; i < sizeof(s_tool_exec) / sizeof(lean_skill_exec_cb); i++) {
    hadnle = s_tool_exec[i](input, output, prov_data);
    if (hadnle) {
      break;
    }
  }
  return hadnle;
}