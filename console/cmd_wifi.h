#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CMD_WIFI_ON_CONNECTED,
  CMD_WIFI_ON_DISCONNECTED,
  CMD_WIFI_ON_GOTIP
} cmd_wifi_event;

typedef void (*cmd_wifi_event_cb)(cmd_wifi_event event, void *argv);

/**
 * @brief 连接wifi
 * @cmd: wifi_join "mi" "0987654321"
 *
 */
void cmd_register_wifi(cmd_wifi_event_cb cb);

#ifdef __cplusplus
}
#endif
