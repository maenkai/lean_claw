#include "driver/ledc.h"

#include "skill.h"
#include "tool_cid_def.h"

bool tool_pwm_exec(const lean_exec_ctx* msg, const lean_exec_input* input, lean_exec_output* output, void* prov_data) {
  switch (input->id) {
  case TOOL_CID_PWM_INIT: {
    esp_err_t           err        = ESP_OK;
    ledc_timer_config_t ledc_timer = {
      .duty_resolution = lean_exec_param_number_get(input, 0),
      .freq_hz         = lean_exec_param_number_get(input, 1),
      .speed_mode      = lean_exec_param_number_get(input, 2),
      .timer_num       = lean_exec_param_number_get(input, 3),
      .clk_cfg         = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&ledc_timer);

    if (ESP_OK != err) {
      lean_exec_result_set_success(output, err == ESP_OK);
      return true;
    }

    ledc_channel_config_t led_gp_channel = { 0 };
    led_gp_channel.channel               = lean_exec_param_number_get(input, 4);
    led_gp_channel.gpio_num              = lean_exec_param_number_get(input, 5);
    led_gp_channel.speed_mode            = ledc_timer.speed_mode;
    led_gp_channel.timer_sel             = ledc_timer.timer_num;
    err                                  = ledc_channel_config(&led_gp_channel);
    ledc_fade_func_install(0);
    lean_exec_result_set_success(output, err == ESP_OK);
    return true;
  }

  case TOOL_CID_PWM_SET: {
    esp_err_t err = ledc_set_fade_time_and_start(
      lean_exec_param_number_get(input, 0), // speed_mode
      lean_exec_param_number_get(input, 1), // channel
      lean_exec_param_number_get(input, 2), // duty
      lean_exec_param_number_get(input, 3), // fade time
      false);
    lean_exec_result_set_success(output, err == ESP_OK);
    return true;
  }
  }

  return false;
}