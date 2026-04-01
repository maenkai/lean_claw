
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"

#include "executor.h"
#include "utils_log.h"

#define TAG "agent_exec"

typedef struct {
  lean_exec_ctx msg;
  lean_exec_cb   exec_cb;
  void*          prov_data;
} _lean_exec_handle;

/**
 * 指令数据设计如下：
 * //调用
 * "func":
 * [
 *   {
 *     "seq": 3,
 *     "id": 1,
 *     "param":["hello world"]
 *    },
 *    {
 *      "seq": 4,
 *      "id": 2,
 *      "para":[true]
 *      "func":
 *          [{
 *              "seq": 5,
 *              "id": 1,
 *              "param":["hello world"]
 *          }]
 *    }
 *  ]
 *
 * //返回结果
 * res:[
 *    {
 *     "seq": 3,
 *     "id": 1,
 *      "success":true,
 *      "val":[11]
 *    }
 * ]
 */

/**
 * @brief 执行句柄创建
 *
 * @param msg
 * @param cb
 * @param prov_data
 * @return * lean_exec_handle
 */
lean_exec_handle lean_exec_create(lean_exec_cb cb, void* prov_data) {
  _lean_exec_handle* handle = (_lean_exec_handle*)calloc(1, sizeof(_lean_exec_handle));
  handle->exec_cb           = cb;
  handle->prov_data         = prov_data;
  return handle;
}

/**
 * @brief  设置执行句柄的agent_msg
 *
 * @param hd
 * @param msg
 */
void lean_exec_set_ctx(lean_exec_handle hd, const lean_exec_ctx* msg) {
  _lean_exec_handle* handle = (_lean_exec_handle*)hd;
  handle->msg               = *msg;
}

/**
 * @brief 根据指令集调用skill
 *
 * @param command
 * @param skill
 */
void lean_exec_function_call(lean_exec_handle handle, cJSON* function, cJSON* op_result) {
  _lean_exec_handle* object = (_lean_exec_handle*)handle;
  int                size   = cJSON_GetArraySize(function);
  for (int i = 0; i < size; i++) {
    cJSON* func_object = cJSON_GetArrayItem(function, i);
    if (func_object->type != cJSON_Object) {
      LEAN_ERROR(TAG, "invalid function format");
      break;
    }

    lean_exec_input  in  = { 0 };
    lean_exec_output out = { 0 };
    in.seq               = cJSON_GetNumberValue(cJSON_GetObjectItem(func_object, "seq"));
    in.id                = cJSON_GetNumberValue(cJSON_GetObjectItem(func_object, "id"));
    in.param             = cJSON_GetObjectItem(func_object, "param");

    if (op_result) {
      out.res = lean_exec_result_object_create(in.id, in.seq);
    }

    bool res = object->exec_cb(&object->msg, &in, &out, object->prov_data);

    if (!res) {
      LEAN_WARN(TAG, "not handle func id %d", in.id);
    }

    if (op_result) {
      cJSON_AddItemToArray(op_result, out.res);
    }
  }
}

/**
 * @brief skill参数获取
 *
 * @param param
 */
int lean_exec_param_size_get(const lean_exec_input* input) {
  return cJSON_GetArraySize(input->param);
}
double lean_exec_param_double_get(const lean_exec_input* input, int index) {
  return cJSON_GetNumberValue(cJSON_GetArrayItem(input->param, index));
}
int lean_exec_param_number_get(const lean_exec_input* input, int index) {
  return cJSON_GetNumberValue(cJSON_GetArrayItem(input->param, index));
}
const char* lean_exec_param_string_get(const lean_exec_input* input, int index) {
  return cJSON_GetStringValue(cJSON_GetArrayItem(input->param, index));
}

bool lean_exec_param_boolean_get(const lean_exec_input* input, int index) {
  return cJSON_IsTrue(cJSON_GetArrayItem(input->param, index));
}

/**
 * @brief 创建一个结果对象,用户主动回复数据
 *
 * @param id
 * @param seq
 * @return cJSON*
 */
void* lean_exec_result_object_create(uint32_t id, uint32_t seq) {
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
void lean_exec_result_set_success(lean_exec_output* output, bool boolean) {
  if (output->res) {
    cJSON_AddBoolToObject(output->res, "success", boolean);
  }
}

/**
 * @brief 告知模型函数的返回结果
 *
 * @param output->res
 */
int lean_exec_result_value_size_get(lean_exec_output* output) {
  return cJSON_GetArraySize(cJSON_GetObjectItem(output->res, "val"));
}

void lean_exec_result_value_double_append(lean_exec_output* output, double number) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateNumber(number));
}
void lean_exec_result_value_number_append(lean_exec_output* output, int number) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateNumber(number));
}
void lean_exec_result_value_string_append(lean_exec_output* output, char* string) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateString(string));
}

void lean_exec_result_value_boolean_append(lean_exec_output* output, bool boolean) {
  cJSON* val = cJSON_GetObjectItem(output->res, "val");
  cJSON_AddItemToArray(val, cJSON_CreateBool(boolean));
}