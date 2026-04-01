#pragma once
#include "skill.h"
#include "cJSON.h"

typedef struct {
  void* agent;
} lean_exec_ctx;

typedef struct {
  uint32_t id;
  uint32_t seq;
  void*    param;
} lean_exec_input;

typedef struct {
  void* res;
} lean_exec_output;

typedef void* lean_exec_handle;

/**
 * @brief 指令回调
 * @param id 调用的指令id
 * @param seq 指令的序列号
 * @param param 指令的参数
 * @param result 指令的结果
 */
typedef bool (*lean_exec_cb)(const lean_exec_ctx* msg, const lean_exec_input* param, lean_exec_output* res, void* prov_data);

/**
 * @brief 执行句柄创建
 *
 * @param msg
 * @param cb
 * @param prov_data
 * @return * lean_exec_handle
 */
lean_exec_handle lean_exec_create(lean_exec_cb cb, void* prov_data);

/**
 * @brief  设置执行句柄的上下文信息
 *
 * @param hd
 * @param msg
 */
void lean_exec_set_ctx(lean_exec_handle hd, const lean_exec_ctx* msg);

/**
 * @brief 根据指令集调用skill
 *
 * @param func
 * @param skill
 */
void lean_exec_function_call(lean_exec_handle handle, cJSON* func, cJSON* op_result);

/**
 * @brief skill参数获取
 *
 * @param param
 */
int         lean_exec_param_size_get(const lean_exec_input* input);
double      lean_exec_param_double_get(const lean_exec_input* input, int index);
int         lean_exec_param_number_get(const lean_exec_input* input, int index);
const char* lean_exec_param_string_get(const lean_exec_input* input, int index);
bool        lean_exec_param_boolean_get(const lean_exec_input* input, int index);

/**
 * @brief 创建一个结果对象,用户主动回复数据
 *
 * @param id
 * @param seq
 * @return cJSON*
 */
void* lean_exec_result_object_create(uint32_t id, uint32_t seq);

/**
 * @brief 告知模型函数是否调用成功
 *
 * @param result
 */
void lean_exec_result_set_success(lean_exec_output* output, bool boolean);

/**
 * @brief 告知模型函数的返回结果
 *
 * @param res_item
 */
int  lean_exec_result_value_size_get(lean_exec_output* output);
void lean_exec_result_value_double_append(lean_exec_output* output, double number);
void lean_exec_result_value_number_append(lean_exec_output* output, int number);
void lean_exec_result_value_string_append(lean_exec_output* output, char* string);
void lean_exec_result_value_boolean_append(lean_exec_output* output, bool boolean);
