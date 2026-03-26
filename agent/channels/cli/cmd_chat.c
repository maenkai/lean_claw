#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "cmd_chat.h"

static struct {
  struct arg_str* user_content;
  struct arg_end* end;
} chat_args;

static lean_cmd_chat_pipeline_cb s_chat_pipeline_cb = NULL;

static int chat(int argc, char** argv) {
  int nerrors = arg_parse(argc, argv, (void**)&chat_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, chat_args.end, argv[0]);
    return 1;
  }

  if (s_chat_pipeline_cb) {
    s_chat_pipeline_cb(chat_args.user_content->sval[0], strlen(chat_args.user_content->sval[0]));
  }
  return 0;
}

void lean_cmd_register_chat(lean_cmd_chat_pipeline_cb cb) {
  chat_args.user_content = arg_str1(NULL, NULL, "<content>", "Content of the conversation");
  chat_args.end          = arg_end(1);

  const esp_console_cmd_t join_cmd = {
    .command  = "chat",
    .help     = "chat input",
    .hint     = NULL,
    .func     = &chat,
    .argtable = &chat_args
  };

  ESP_ERROR_CHECK(esp_console_cmd_register(&join_cmd));
  s_chat_pipeline_cb = cb;
}
