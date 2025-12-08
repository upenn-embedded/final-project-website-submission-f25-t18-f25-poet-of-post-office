#include "audio_frontend.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "AF";



void af_compute_log_mel(const int16_t* audio,
                        float* out_features)
{

    float frame[AF_FRAME_LEN];

    for (int f = 0; f < AF_NUM_FRAMES; ++f) {
        int start = f * AF_FRAME_STEP;

        for (int n = 0; n < AF_FRAME_LEN; ++n) {
            int idx = start + n;
            float x = 0.0f;
            if (idx < AF_CLIP_SAMPLES) {
                x = (float)audio[idx] / 32768.0f;
            }

            float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * n / (AF_FRAME_LEN - 1));
            frame[n] = x * w;
        }


        float band_energy[AF_NUM_MEL_BINS];
        for (int m = 0; m < AF_NUM_MEL_BINS; ++m) {
            band_energy[m] = 0.0f;
        }

        for (int n = 0; n < AF_FRAME_LEN; ++n) {
            float x = frame[n];
            float e = x * x;

            int band = (n * AF_NUM_MEL_BINS) / AF_FRAME_LEN;
            if (band < 0) band = 0;
            if (band >= AF_NUM_MEL_BINS) band = AF_NUM_MEL_BINS - 1;

            band_energy[band] += e;
        }


        for (int m = 0; m < AF_NUM_MEL_BINS; ++m) {
            float e = band_energy[m];
            // 避免 log(0)
            e = logf(e + 1e-6f);
            out_features[f * AF_NUM_MEL_BINS + m] = e;
        }
    }


    ESP_LOGD(TAG, "af_compute_log_mel done");
}
