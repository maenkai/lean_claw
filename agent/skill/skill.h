#pragma once
#include <stdint.h>
#include <stdbool.h>

// 参数封装
#define skill_param(...) "[" #__VA_ARGS__ "]"

typedef void* lean_skill_handle;

typedef struct {
  uint32_t    id;
  const char* desc;
  const char* param;
  const char* ret;
} lean_skill_config;

typedef struct {
  uint32_t          id;
  uint32_t          seq;
  void*             param;
  lean_skill_handle skill;
} lean_skill_input;

typedef struct {
  void* res;
} lean_skill_output;

/**
 * @brief 指令回调
 * @param id 调用的指令id
 * @param seq 指令的序列号
 * @param param 指令的参数
 * @param result 指令的结果
 */
typedef bool (*lean_skill_exec_cb)(const lean_skill_input* input, lean_skill_output* output, void* prov_data);

/**
 * @brief 创建skill表
 *
 * @return lean_skill_handle
 */
lean_skill_handle lean_skill_create(lean_skill_exec_cb cb, void* prov_data);

/**
 * @brief 执行skill
 *
 * @param hd
 * @param input
 * @param output
 * @return true
 * @return false
 */
bool lean_skill_exec(lean_skill_handle hd, const lean_skill_input* input, lean_skill_output* output);

/**
 * @brief 添加skill
 *
 * @param hd
 * @param config
 * @example
 *  config = {
 *  id = 1...max 不可重复
 *  desc = “open file”
 *  param = skill_param(int#整形,bool#布尔,str#字符串,f#浮点值)
 *  ret = "返回描述"
 * }
 */
void lean_skill_append(lean_skill_handle hd, const lean_skill_config* config, uint32_t coutns);

/**
 * @brief 获取skill的json对象
 *
 * @param hd
 * @return cJSON*
 */
char* lean_skill_get_jsonstring(lean_skill_handle hd);

/**
 * @brief skill参数获取
 *
 * @param param
 */
int         lean_skill_param_size_get(const lean_skill_input* input);
double      lean_skill_param_double_get(const lean_skill_input* input, int index);
int         lean_skill_param_number_get(const lean_skill_input* input, int index);
const char* lean_skill_param_string_get(const lean_skill_input* input, int index);
bool        lean_skill_param_boolean_get(const lean_skill_input* input, int index);

/**
 * @brief 创建一个结果对象,用户主动回复数据
 *
 * @param id
 * @param seq
 * @return cJSON*
 */
void* lean_skill_result_object_create(uint32_t id, uint32_t seq);

/**
 * @brief 告知模型函数是否调用成功
 *
 * @param result
 */
void lean_skill_result_set_success(lean_skill_output* output, bool boolean);

/**
 * @brief 告知模型函数的返回结果
 *
 * @param res_item
 */
int  lean_skill_result_value_size_get(lean_skill_output* output);
void lean_skill_result_value_double_append(lean_skill_output* output, double number);
void lean_skill_result_value_number_append(lean_skill_output* output, int number);
void lean_skill_result_value_string_append(lean_skill_output* output, char* string);
void lean_skill_result_value_boolean_append(lean_skill_output* output, bool boolean);