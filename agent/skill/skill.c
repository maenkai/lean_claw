
#include "stdbool.h"
#include "stdlib.h"
#include "cJSON.h"

#include "skill.h"

/**
 * {
 *   "skill":[
 *     {
 *     "id": number,
 *     "desc":"printf"
 *     "param":"["string", number, bool]",
 *     "func":true,//是否需要传入子函数
 *     }
 *    ]
 *
 * }
 *
 */

typedef struct {
  const char* desc;
  cJSON*      skill;
} _lean_skill_handle;

/**
 * @brief 创建skill表
 *
 * @return lean_skill_handle
 */
lean_skill_handle lean_skill_create(const char* desc) {
  _lean_skill_handle* hd = calloc(1, sizeof(_lean_skill_handle));
  hd->skill              = cJSON_CreateObject();
  hd->desc               = desc;
  cJSON_AddItemToObject(hd->skill, "skill", cJSON_CreateArray());
  return hd;
}

/**
 * @brief 添加skill
 *
 * @param hd
 * @param config
 * @example
 *  id = 0...max 不可重复
 *  desc = “open light”
 *  param = "[]" or "[number, string , bool]"
 */
void lean_skill_append(lean_skill_handle hd, const lean_skill_config* config, uint32_t coutns) {
  _lean_skill_handle* core_hd     = (_lean_skill_handle*)hd;
  cJSON*              skill_array = cJSON_GetObjectItem(core_hd->skill, "skill");
  for (int i = 0; i < coutns; i++) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", config[i].id);
    cJSON_AddStringToObject(item, "desc", config[i].desc);
    cJSON_AddStringToObject(item, "param", config[i].param);
    cJSON_AddStringToObject(item, "ret", config[i].ret);
    cJSON_AddItemToArray(skill_array, item);
  }
}

/**
 * @brief 获取skill的json对象
 *
 * @param hd
 * @return cJSON*
 */
char* lean_skill_get_jsonstring(lean_skill_handle hd) {
  _lean_skill_handle* core_hd = (_lean_skill_handle*)hd;
  return cJSON_PrintUnformatted(core_hd->skill);
}
