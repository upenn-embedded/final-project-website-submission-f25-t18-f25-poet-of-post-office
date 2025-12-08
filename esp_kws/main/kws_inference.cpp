#include "kws_inference.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "model_data.h"
#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"


static const char* TAG = "KWS";

const char* g_kws_class_names[KWS_NUM_CLASSES] = {
    "Jarvis",
    "Other"
};


constexpr int kTensorArenaSize = 100 * 1024;
static uint8_t g_tensor_arena[kTensorArenaSize];

static tflite::MicroInterpreter* g_interpreter = nullptr;
static TfLiteTensor* g_input  = nullptr;
static TfLiteTensor* g_output = nullptr;

bool kws_init(void)
{
    ESP_LOGI(TAG, "Initializing TFLite Micro...");

    const tflite::Model* model = tflite::GetModel(g_kws_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema %d not equal to supported %d",
                 model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }


    static tflite::MicroMutableOpResolver<15> resolver;


    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();

    resolver.AddMaxPool2D();

    resolver.AddFullyConnected();

    resolver.AddReshape();

    resolver.AddSoftmax();
    resolver.AddRelu();   

    resolver.AddMul();
    resolver.AddAdd();

    resolver.AddShape(); 
    resolver.AddStridedSlice(); 
    resolver.AddPad();
    resolver.AddPack();      
    resolver.AddMean();  

    static tflite::MicroInterpreter static_interpreter(
        model,
        resolver,
        g_tensor_arena,
        kTensorArenaSize
    );

    g_interpreter = &static_interpreter;

    TfLiteStatus alloc_status = g_interpreter->AllocateTensors();
    if (alloc_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        return false;
    }

    g_input  = g_interpreter->input(0);
    g_output = g_interpreter->output(0);

    ESP_LOGI(TAG, "TFLM initialized. Input dims:");
    for (int i = 0; i < g_input->dims->size; ++i) {
        printf(" %d", g_input->dims->data[i]);
    }
    printf("\n");

    return true;
}

bool kws_infer_one(const float* features,
                   int feature_len,
                   int* out_class,
                   float* out_scores)
{
    if (!g_interpreter || !g_input || !g_output) {
        ESP_LOGE(TAG, "kws_init() not called yet");
        return false;
    }


    int expected_len = 1;
    for (int i = 0; i < g_input->dims->size; ++i) {
        expected_len *= g_input->dims->data[i];
    }


    if (feature_len != expected_len) {
        ESP_LOGW(TAG, "feature_len=%d, but input tensor expects %d",
                 feature_len, expected_len);
    }

    if (g_input->type != kTfLiteInt8 || g_output->type != kTfLiteInt8) {
        ESP_LOGE(TAG, "Expected int8 input/output tensors");
        return false;
    }

    float in_scale = g_input->params.scale;
    int   in_zp    = g_input->params.zero_point;
    int8_t* in_data = g_input->data.int8;

    int copy_len = (feature_len < expected_len) ? feature_len : expected_len;

    for (int i = 0; i < copy_len; ++i) {
        float f = features[i];
        int32_t q = (int32_t)lroundf(f / in_scale) + in_zp;
        if (q < -128) q = -128;
        if (q > 127)  q = 127;
        in_data[i] = (int8_t)q;
    }

    for (int i = copy_len; i < expected_len; ++i) {
        in_data[i] = (int8_t)in_zp;
    }


    if (g_interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke() failed");
        return false;
    }

    float out_scale = g_output->params.scale;
    int   out_zp    = g_output->params.zero_point;
    int8_t* out_q   = g_output->data.int8;

    int num_classes = g_output->dims->data[g_output->dims->size - 1];
    if (num_classes != KWS_NUM_CLASSES) {
        ESP_LOGW(TAG, "Model num_classes=%d, but KWS_NUM_CLASSES=%d",
                 num_classes, KWS_NUM_CLASSES);
    }

    int   max_idx    = 0;
    float max_score  = -1e9f;

    for (int i = 0; i < num_classes; ++i) {
        float p = (out_q[i] - out_zp) * out_scale; 
        if (out_scores && i < KWS_NUM_CLASSES) {
            out_scores[i] = p;
        }
        if (p > max_score) {
            max_score = p;
            max_idx   = i;
        }
    }

    if (out_class) {
        *out_class = max_idx;
    }

    return true;
}
