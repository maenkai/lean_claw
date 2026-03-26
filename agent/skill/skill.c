
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
  void*              prov_data;
  cJSON*             skill;
  lean_skill_exec_cb cb;
} _lean_skill_handle;

/**
 * @brief 创建skill表
 *
 * @return lean_skill_handle
 */
lean_skill_handle lean_skill_create(lean_skill_exec_cb cb, void* prov_data) {
  _lean_skill_handle* hd = calloc(1, sizeof(_lean_skill_handle));
  hd->skill              = cJSON_CreateObject();
  hd->prov_data          = prov_data;
  hd->cb                 = cb;
  cJSON_AddItemToObject(hd->skill, "skill", cJSON_CreateArray());
  return hd;
}

/**
 * @brief 执行skill
 *
 * @param hd
 * @param input
 * @param output
 * @return true
 * @return false
 */
bool lean_skill_exec(lean_skill_handle hd, const lean_skill_input* input, lean_skill_output* output) {
  _lean_skill_handle* core_hd = (_lean_skill_handle*)hd;
  return core_hd->cb(input, output, core_hd->prov_data);
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

/**
 * @brief skill参数获取
 *
 * @param param
 */
int lean_skill_param_size_get(const lean_skill_input* input) {
  return cJSON_GetArraySize(input->param);
}
double lean_skill_param_double_get(const lean_skill_input* input, int index) {
  return cJSON_GetNumberValue(cJSON_GetArrayItem(input->param, index));
}
int lean_skill_param_number_get(const lean_skill_input* input, int index) {
  return cJSON_GetNumberValue(cJSON_GetArrayItem(input->param, index));
}
const char* lean_skill_param_string_get(const lean_skill_input* input, int index) {
  return cJSON_GetStringValue(cJSON_GetArrayItem(input->param, index));
}

bool lean_skill_param_boolean_get(const lean_skill_input* input, int index) {
  return cJSON_IsTrue(cJSON_GetArrayItem(input->param, index));
}

/**
 * @brief 释放函数资源
 *
 * @param func
 */
void lean_skill_func_delete(void* func) {
  if (func) {
    cJSON_Delete(func);
  }
}

/**
 * @brief 创建一个结果对象,用户主动回复数据
 *
 * @param id
 * @param seq
 * @return cJSON*
 */
void* lean_skill_result_object_create(uint32_t id, uint32_t seq) {
  cJSON* res = cJSON_CreateObject();
  cJSON_AddNumberToObject(res, "id", id);
  cJSON_AddNumberToObject(res, "seq", seq);
  cJSON_AddItemToObject(res, "val", cJSON_CreateArray());
  return res;
}

/**
 * @brief 告知模型函数是否调用成功
 *
 * @param result
 */
void lean_skill_result_set_success(lean_skill_output* output, bool boolean) {
  if (output->res) {
    cJSON_AddBoolToObject(output->res, "success", boolean);
  }
}

/**
 * @brief 告知模型函数的返回结果
 *
 * @param output->res
 */
int lean_skill_result_value_size_get(lean_skill_output* output) {
  return cJSON_GetArraySize(cJSON_GetObjectItem(output->res, "val"));
}

void lean_skill_result_value_double_append(lean_skill_output* output, double number) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateNumber(number));
}
void lean_skill_result_value_number_append(lean_skill_output* output, int number) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateNumber(number));
}
void lean_skill_result_value_string_append(lean_skill_output* output, char* string) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateString(string));
}

void lean_skill_result_value_boolean_append(lean_skill_output* output, bool boolean) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateBool(boolean));
}