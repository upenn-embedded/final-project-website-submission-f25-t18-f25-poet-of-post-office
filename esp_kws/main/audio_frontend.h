#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_SAMPLE_RATE     16000
#define AF_CLIP_SAMPLES    16000

#define AF_FRAME_LEN       640    // 和训练脚本保持一致
#define AF_FRAME_STEP      320
#define AF_NUM_FRAMES      49
#define AF_NUM_MEL_BINS    40

// 输出特征尺寸 = 49 * 40
#define AF_FEATURE_SIZE    (AF_NUM_FRAMES * AF_NUM_MEL_BINS)

// audio: 1 秒 16kHz int16 PCM
// out_features: float32[AF_FEATURE_SIZE], 布局 [time * freq]
void af_compute_log_mel(const int16_t* audio,
                        float* out_features);

#ifdef __cplusplus
}
#endif
