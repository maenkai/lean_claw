#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "skill.h"
#include "executor.h"
#include "tool_cid_def.h"

bool tool_thread_exec(const lean_exec_ctx* msg, const lean_exec_input* input, lean_exec_output* output, void* prov_data) {
  switch (input->id) {
    case TOOL_CID_SYSTEM_SLEEP: {
      vTaskDelay(lean_exec_param_number_get(input, 0) / portTICK_PERIOD_MS);
      lean_exec_result_set_success(output, true);
      return true;
    }
  }

  return false;
}