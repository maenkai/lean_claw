/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "cmd_wifi.h"
#include "utils_log.h"

#define JOIN_TIMEOUT_MS (10000)
#define CONNECTED_BIT   BIT0

static cmd_wifi_event_cb  s_wifi_event_cb;
static EventGroupHandle_t s_wifi_event_group;
static bool               wifi_join(const char* ssid, const char* pass, int timeout_ms);

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    if (s_wifi_event_cb) {
      s_wifi_event_cb(CMD_WIFI_ON_DISCONNECTED, NULL);
    }
    xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    if (s_wifi_event_cb) {
      s_wifi_event_cb(CMD_WIFI_ON_GOTIP, NULL);
    }
  }
}

static void initialise_wifi(void) {
  esp_log_level_set("wifi", ESP_LOG_WARN);
  static bool initialized = false;
  if (initialized) {
    return;
  }
  ESP_ERROR_CHECK(esp_netif_init());
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
  assert(ap_netif);
  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());
  initialized = true;

  wifi_config_t wifi_config = { 0 };
  if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK && wifi_config.sta.ssid[0] != '\0') {
    LEAN_INFO(__func__, "Connecting to '%s'", wifi_config.sta.ssid);
    wifi_join((const char*)wifi_config.sta.ssid, (const char*)wifi_config.sta.password, 0);
  }
}

static bool wifi_join(const char* ssid, const char* pass, int timeout_ms) {
  initialise_wifi();
  wifi_config_t wifi_config = { 0 };
  strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  if (pass) {
    strlcpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  esp_wifi_connect();

  if (!timeout_ms) {
    return true;
  }

  int bits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT,
                                 pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
  return (bits & CONNECTED_BIT) != 0;
}

/** Arguments used by 'join' function */
static struct
{
  struct arg_int* timeout;
  struct arg_str* ssid;
  struct arg_str* password;
  struct arg_end* end;
} join_args;

static int connect(int argc, char** argv) {
  int nerrors = arg_parse(argc, argv, (void**)&join_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, join_args.end, argv[0]);
    return 1;
  }
  LEAN_INFO(__func__, "Connecting to '%s'",
             join_args.ssid->sval[0]);

  /* set default value*/
  if (join_args.timeout->count == 0) {
    join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
  }

  bool connected = wifi_join(join_args.ssid->sval[0],
                             join_args.password->sval[0],
                             join_args.timeout->ival[0]);
  if (!connected) {
    LEAN_WARN(__func__, "Connection timed out");
    return 1;
  }
  LEAN_INFO(__func__, "Connected");
  return 0;
}

void cmd_register_wifi(cmd_wifi_event_cb cb) {
  join_args.timeout  = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
  join_args.ssid     = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
  join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
  join_args.end      = arg_end(2);

  const esp_console_cmd_t join_cmd = {
    .command  = "wifi_join",
    .help     = "Join WiFi AP as a station",
    .hint     = NULL,
    .func     = &connect,
    .argtable = &join_args
  };

  s_wifi_event_cb = cb;
  ESP_ERROR_CHECK(esp_console_cmd_register(&join_cmd));
  initialise_wifi();
}
