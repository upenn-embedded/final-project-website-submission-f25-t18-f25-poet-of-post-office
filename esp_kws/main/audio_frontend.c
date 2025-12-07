#include "audio_frontend.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "AF";

/*
 * 轻量级“伪 log-mel”前端
 *
 * 思路：
 *   - 仍然按 49 帧、每帧 AF_FRAME_LEN=640 点来扫 1 秒音频
 *   - 不做 FFT
 *   - 每一帧里，把时间轴平均分成 AF_NUM_MEL_BINS 段（40 段）
 *   - 每一段累加 x^2（能量），最后取 log(energy)
 *   - 得到的特征 shape 仍是 [49, 40]，可以直接喂给当前模型
 *
 * 注意：这和 Python 那边用的真正 log-mel 有差异，所以
 *       目前模型是用“真 mel”训练的，在板子上跑的特征是“伪 mel”，
 *       准确率可能会掉。后面如果你愿意，可以把 train_kws.py 也改成这个方法再重训。
 */

void af_compute_log_mel(const int16_t* audio,
                        float* out_features)
{
    // 临时数组：存这一帧的窗函数+归一化后的样本
    float frame[AF_FRAME_LEN];

    for (int f = 0; f < AF_NUM_FRAMES; ++f) {
        int start = f * AF_FRAME_STEP;

        // 1. 拷贝一帧数据 + 简单 Hann 窗
        for (int n = 0; n < AF_FRAME_LEN; ++n) {
            int idx = start + n;
            float x = 0.0f;
            if (idx < AF_CLIP_SAMPLES) {
                x = (float)audio[idx] / 32768.0f;
            }
            // Hann 窗
            float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * n / (AF_FRAME_LEN - 1));
            frame[n] = x * w;
        }

        // 2. 把这一帧按时间分成 40 个“频带”，每个频带统计能量
        float band_energy[AF_NUM_MEL_BINS];
        for (int m = 0; m < AF_NUM_MEL_BINS; ++m) {
            band_energy[m] = 0.0f;
        }

        for (int n = 0; n < AF_FRAME_LEN; ++n) {
            float x = frame[n];
            float e = x * x;

            // 根据时间位置，把样本分到某个“band”
            int band = (n * AF_NUM_MEL_BINS) / AF_FRAME_LEN;
            if (band < 0) band = 0;
            if (band >= AF_NUM_MEL_BINS) band = AF_NUM_MEL_BINS - 1;

            band_energy[band] += e;
        }

        // 3. 对每个 band 取对数，写入最终特征
        for (int m = 0; m < AF_NUM_MEL_BINS; ++m) {
            float e = band_energy[m];
            // 避免 log(0)
            e = logf(e + 1e-6f);
            out_features[f * AF_NUM_MEL_BINS + m] = e;
        }
    }

    // 这里可以做一个简单的全局归一化（可选），比如减去均值/除以最大值
    // 目前先保持和训练侧“同量级”即可，有需要我们再一起调。
    ESP_LOGD(TAG, "af_compute_log_mel done");
}
