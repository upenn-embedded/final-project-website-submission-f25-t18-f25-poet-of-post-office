#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


#define KWS_NUM_CLASSES 2


extern const char* g_kws_class_names[KWS_NUM_CLASSES];


bool kws_init(void);

bool kws_infer_one(const float* features,
                   int feature_len,
                   int* out_class,
                   float* out_scores);

#ifdef __cplusplus
}
#endif
