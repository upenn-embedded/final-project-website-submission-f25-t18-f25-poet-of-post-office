#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_SAMPLE_RATE     16000
#define AF_CLIP_SAMPLES    16000

#define AF_FRAME_LEN       640    
#define AF_FRAME_STEP      320
#define AF_NUM_FRAMES      49
#define AF_NUM_MEL_BINS    40

#define AF_FEATURE_SIZE    (AF_NUM_FRAMES * AF_NUM_MEL_BINS)


void af_compute_log_mel(const int16_t* audio,
                        float* out_features);

#ifdef __cplusplus
}
#endif
