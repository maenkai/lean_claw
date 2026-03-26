#pragma once
#include <stdint.h>
#include <stdbool.h>

/*
 1.开通DashScope即获赠总计2,000,000 tokens限时免费使用额度，有效期180天。
 2.以下条件任何一个超出都会触发限流：
 调用频次 ≤ 500 QPM，每分钟不超过500次API调用；
 Token消耗 ≤ 500,000 TPM，每分钟消耗的token数目不超过500,000。
*/
#define APT_QWEN_MODEL_TURBO "qwen-turbo"
/**
 * 其他计费详情查看:https://help.aliyun.com/zh/dashscope/developer-reference/tongyi-thousand-questions-metering-and-billing?spm=a2c4g.11186623.0.0.36e546c1LmSrmb#e3696ff0cfd7u
 */
#define APT_QWEN_MODEL_PLUS            "qwen-plus"
#define APT_QWEN_MODEL_MAX             "qwen-max"
#define APT_QWEN_MODEL_MAX_0403        "qwen-max-0403"
#define APT_QWEN_MODEL_MAX_0107        "qwen-max-0107"
#define APT_QWEN_MODEL_MAX_LONGCONTEXT "qwen-max-longcontext"
