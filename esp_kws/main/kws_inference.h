#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// KWS 类别数（现在是二分类：Jarvis / Other）
#define KWS_NUM_CLASSES 2

// 类别名（在 cpp 里定义）
extern const char* g_kws_class_names[KWS_NUM_CLASSES];

// 初始化 TFLM，加载模型，分配 tensor arena
bool kws_init(void);

// 单次推理：
//   features: 长度 = feature_len 的 float32，一般是 49*40
//   out_class: 输出类别 id (0 ~ KWS_NUM_CLASSES-1)
//   out_scores: float32[KWS_NUM_CLASSES]，存每一类的概率；可以为 NULL
bool kws_infer_one(const float* features,
                   int feature_len,
                   int* out_class,
                   float* out_scores);

#ifdef __cplusplus
}
#endif
