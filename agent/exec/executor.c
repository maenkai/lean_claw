
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"

#include "skill.h"
#include "utils_log.h"

#define TAG "agent_exec"

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
 * @brief 根据指令集调用skill
 *
 * @param command
 * @param skill
 */
void lean_executor_function_running(cJSON* function, lean_skill_handle skill, cJSON* op_result) {
  int size = cJSON_GetArraySize(function);
  for (int i = 0; i < size; i++) {
    cJSON* func_object = cJSON_GetArrayItem(function, i);
    if (func_object->type != cJSON_Object) {
      LEAN_ERROR(TAG, "invalid function format");
      break;
    }

    lean_skill_input  lean_skill_input = { 0 };
    lean_skill_output skill_output     = { 0 };
    lean_skill_input.skill             = skill;
    lean_skill_input.seq               = cJSON_GetNumberValue(cJSON_GetObjectItem(func_object, "seq"));
    lean_skill_input.id                = cJSON_GetNumberValue(cJSON_GetObjectItem(func_object, "id"));
    lean_skill_input.param             = cJSON_GetObjectItem(func_object, "param");

    if (op_result) {
      skill_output.res = lean_skill_result_object_create(lean_skill_input.id, lean_skill_input.seq);
    }

    bool res = lean_skill_exec(skill, &lean_skill_input, &skill_output);

    if (!res) {
      LEAN_WARN(TAG, "not handle func id %d", lean_skill_input.id);
    }

    if (op_result) {
      cJSON_AddItemToArray(op_result, skill_output.res);
    }
  }
}
