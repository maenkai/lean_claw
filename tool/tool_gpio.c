#include "driver/gpio.h"
#include "skill.h"
#include "tool_cid_def.h"

bool tool_gpio_exec(const lean_skill_input* input, lean_skill_output* output, void* prov_data) {
  switch (input->id) {
  case TOOL_CID_GPIO_INIT: {
    gpio_config_t io_conf;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = lean_skill_param_number_get(input, 0) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << lean_skill_param_number_get(input, 1));
    io_conf.pull_down_en = lean_skill_param_boolean_get(input, 2);
    io_conf.pull_up_en   = lean_skill_param_boolean_get(input, 3);
    esp_err_t err        = gpio_config(&io_conf);
    lean_skill_result_set_success(output, err == ESP_OK);
    return true;
  }

  case TOOL_CID_GPIO_SET: {
    esp_err_t err = gpio_set_level(lean_skill_param_number_get(input, 0),
                                   lean_skill_param_number_get(input, 1));
    lean_skill_result_set_success(output, err == ESP_OK);
    return true;
  }

  case TOOL_CID_GPIO_GET: {
    int level = gpio_get_level(lean_skill_param_number_get(input, 0));
    lean_skill_result_set_success(output, true);
    lean_skill_result_value_number_append(output, level);
    return true;
  }
  }

  return false;
}
