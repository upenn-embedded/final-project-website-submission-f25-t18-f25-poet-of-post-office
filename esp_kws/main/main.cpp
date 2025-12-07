#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "driver/i2s_std.h"
#include "driver/uart.h"

#include "audio_frontend.h"
#include "kws_inference.h"

// WiFi + 状态服务器
#include "wifi_connect.h"
#include "Status_server.h"

static const char* TAG = "APP";

/***************************************************
 *              I2S (麦克风)
 **************************************************/
#define I2S_SCK_IO      GPIO_NUM_17
#define I2S_WS_IO       GPIO_NUM_18
#define I2S_SD_IO       GPIO_NUM_16

#define I2S_READ_SAMPLES    512
static i2s_chan_handle_t s_rx_handle = NULL;

/***************************************************
 *              UART (和 ATmega 通讯)
 **************************************************/
#define UART_PORT_NUM   UART_NUM_1
// 这两个引脚请按你实际连线改一下
#define UART_TX_PIN     GPIO_NUM_4   // ESP32-S3 -> ATmega RX
#define UART_RX_PIN     GPIO_NUM_5   // ATmega TX -> ESP32-S3
#define UART_BAUD_RATE  9600
#define UART_BUF_SIZE   256

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(
        UART_PORT_NUM,
        UART_BUF_SIZE * 2,   // RX buffer
        0,                   // no TX buffer
        0,
        NULL,
        0));

    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(
        UART_PORT_NUM,
        UART_TX_PIN,
        UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART initialized: port=%d, TX=%d, RX=%d",
             UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN);
}

/***************************************************
 *          WiFi 状态回调
 ***************************************************/
static void on_wifi_state(wifi_conn_state_t st, void* user)
{
    ESP_LOGI(TAG, "WiFi state -> %d", st);

    switch (st) {
    case WIFI_CONN_STA_OK:
        // STA 连接成功后启动状态服务器
        status_server_start();
        status_set_mode("auto");     // 初始模式
        status_set_voice("none");    // 还没识别到词
        status_set_gesture("unknown");
        break;

    default:
        // 其它状态停掉状态服务器
        status_server_stop();
        break;
    }
}

/***************************************************
 *                I2S 初始化
 ***************************************************/
static void i2s_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S...");

    // 1. 创建 I2S RX 通道（标准模式）
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle));

    // 2. 时钟配置：16 kHz
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AF_SAMPLE_RATE);

    // 3. Slot 配置：Philips I2S，32bit，单声道（和录数据时一致）
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO
    );
    // 麦 SEL 接 GND => 左声道
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // 4. GPIO 配置
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_SCK_IO,
        .ws   = I2S_WS_IO,
        .dout = I2S_GPIO_UNUSED,
        .din  = I2S_SD_IO,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv   = false,
        },
    };

    // 5. 配置标准模式 + 启用通道
    i2s_std_config_t std_cfg = {
        .clk_cfg  = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

    ESP_LOGI(TAG, "I2S initialized: %d Hz", AF_SAMPLE_RATE);
}

/***************************************************
 *       UART 接收任务：从 ATmega 收心率 + 手势
 *  约定串口格式（ATmega 发）：
 *     hr=78,gest=fist\n
 ***************************************************/
static void uart_rx_task(void* arg)
{
    ESP_LOGI(TAG, "uart_rx_task started");

    uint8_t rx_buf[UART_BUF_SIZE];
    char    line[128];
    size_t  line_pos = 0;

    while (1) {
        int len = uart_read_bytes(
            UART_PORT_NUM,
            rx_buf,
            sizeof(rx_buf),
            pdMS_TO_TICKS(20)
        );

        if (len > 0) {
            for (int i = 0; i < len; ++i) {
                char c = (char)rx_buf[i];

                if (c == '\n' || c == '\r') {
                    if (line_pos == 0) {
                        continue;
                    }
                    // 结束一行
                    line[line_pos] = '\0';
                    ESP_LOGI(TAG, "UART RX line: %s", line);

                    // 解析：hr=78,gest=fist
                    int  hr = -1;
                    char gest[32] = {0};

                    // 非严格解析：先找 "hr="，再找 "gest="
                    char* p_hr   = strstr(line, "hr=");
                    char* p_gest = strstr(line, "gest=");
                    if (p_hr) {
                        hr = atoi(p_hr + 3);
                    }
                    if (p_gest) {
                        // 拿到 gest= 之后的内容，拷到 gest[]
                        strncpy(gest, p_gest + 5, sizeof(gest) - 1);
                        // 去掉可能的逗号/空格
                        for (int k = 0; gest[k]; ++k) {
                            if (gest[k] == ',' || gest[k] == ' ' || gest[k] == '\r')
                                gest[k] = '\0';
                        }
                    }

                    // 更新网页显示
                    if (hr >= 0) {
                        char mode_str[32];
                        snprintf(mode_str, sizeof(mode_str), "HR:%d", hr);
                        // 暂时把心率塞在 mode 字段里网页显示
                        status_set_mode(mode_str);
                    }
                    if (gest[0]) {
                        status_set_gesture(gest);
                    }

                    // 清空缓冲，准备下一行
                    line_pos = 0;
                    line[0] = '\0';
                } else {
                    if (line_pos < sizeof(line) - 1) {
                        line[line_pos++] = c;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/***************************************************
 *                KWS 任务
 *  - I2S 连续采样 -> 环形缓冲
 *  - 能量 VAD 检测到语音 -> 取最近 1 秒做推理
 ***************************************************/
#define RING_BUFFER_SAMPLES     (AF_SAMPLE_RATE * 2)   // 2s
#define ENERGY_WINDOW_SAMPLES   1600                  // 0.1s
#define ENERGY_THRESHOLD        0.8f
#define VAD_START_CHUNKS        2
#define VAD_END_CHUNKS          4

static void kws_task(void* arg)
{
    ESP_LOGI(TAG, "kws_task started");

    // 环形缓冲 2 秒 int16 PCM
    static int16_t ring_buffer[RING_BUFFER_SAMPLES];
    int ring_write = 0;
    bool ring_filled = false;

    // I2S 临时缓冲（32-bit 原始数据）
    int32_t i2s_buf[I2S_READ_SAMPLES];

    // 用于能量计算 & 推理窗口
    static int16_t window[AF_CLIP_SAMPLES];     // 1 秒，16k 点
    static float   features[AF_FEATURE_SIZE];   // 49×40

    // VAD 状态
    bool in_speech       = false;
    int  vad_high_count  = 0;
    int  vad_low_count   = 0;

    while (1) {
        /********* 1. I2S 读一小块音频（~32 ms） *********/
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(
            s_rx_handle,
            i2s_buf,
            I2S_READ_SAMPLES * sizeof(int32_t),
            &bytes_read,
            portMAX_DELAY
        );

        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "i2s_channel_read error=%d, bytes=%u", ret, (unsigned)bytes_read);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t samples = bytes_read / sizeof(int32_t);
        if (samples > I2S_READ_SAMPLES) samples = I2S_READ_SAMPLES;

        // 转成 int16，并写入环形缓冲
        for (size_t i = 0; i < samples; ++i) {
            int32_t raw = i2s_buf[i];
            int16_t s   = (int16_t)(raw >> 13);   // 和录数据时保持一致

            ring_buffer[ring_write] = s;
            ring_write = (ring_write + 1) % RING_BUFFER_SAMPLES;
            if (ring_write == 0) {
                ring_filled = true;
            }
        }

        /********* 2. 计算最近 ENERGY_WINDOW_SAMPLES 点的平均能量 *********/
        int energy_win = ENERGY_WINDOW_SAMPLES;
        if (!ring_filled) {
            int filled = ring_write;
            if (filled < energy_win) energy_win = filled;
        }

        if (energy_win > 0) {
            float energy = 0.0f;
            for (int n = 0; n < energy_win; ++n) {
                int idx = (ring_write - 1 - n + RING_BUFFER_SAMPLES) % RING_BUFFER_SAMPLES;
                float x = (float)ring_buffer[idx] / 32768.0f;
                energy += x * x;
            }
            energy /= (float)energy_win;

            bool is_speech_now = (energy > ENERGY_THRESHOLD);

            if (is_speech_now) {
                vad_high_count++;
                vad_low_count = 0;
            } else {
                vad_low_count++;
                vad_high_count = 0;
            }

            if (!in_speech && is_speech_now && vad_high_count >= VAD_START_CHUNKS) {
                in_speech = true;
                ESP_LOGI(TAG, "VAD: speech start (energy=%.6f)", energy);
            }

            if (in_speech && !is_speech_now && vad_low_count >= VAD_END_CHUNKS) {
                in_speech = false;
                ESP_LOGI(TAG, "VAD: speech end (energy=%.6f)", energy);
            }
        }

        /********* 3. 处于说话状态时，取最近 1 秒做一次推理 *********/
        if (in_speech) {
            // 确保至少已经有 1 秒数据
            if (!ring_filled && ring_write < AF_CLIP_SAMPLES) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            // 从环形缓冲取出“最近 1 秒”的 16000 点到 window[]
            int start_idx = (ring_write - AF_CLIP_SAMPLES + RING_BUFFER_SAMPLES) % RING_BUFFER_SAMPLES;
            for (int n = 0; n < AF_CLIP_SAMPLES; ++n) {
                int idx = (start_idx + n) % RING_BUFFER_SAMPLES;
                window[n] = ring_buffer[idx];
            }

            // 4. 提取 log-mel 特征
            af_compute_log_mel(window, features);

            // 5. 模型推理
            int   pred_class = -1;
            float scores[KWS_NUM_CLASSES] = {0};

            bool ok = kws_infer_one(features, AF_FEATURE_SIZE, &pred_class, scores);
            if (!ok) {
                ESP_LOGE(TAG, "kws_infer_one failed");
            } else {
                const char* name = "UNK";
                if (pred_class >= 0 && pred_class < KWS_NUM_CLASSES) {
                    extern const char* g_kws_class_names[KWS_NUM_CLASSES];
                    name = g_kws_class_names[pred_class];
                }

                //ESP_LOGI(TAG, "Prediction: %d (%s)", pred_class, name);

                // 更新网页上的 voice 字段
                status_set_voice(name);

                // 把识别到的词通过 UART 发给 ATmega
                char tx_buf[64];
                int  n = snprintf(tx_buf, sizeof(tx_buf),
                                  "word=%s\n", name);
                if (n > 0) {
                    uart_write_bytes(UART_PORT_NUM, tx_buf, n);
                }
            }

            // 避免一段语音里刷太多 log，适当 sleep
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        // 稍微让出 CPU，避免 WDT
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/***************************************************
 *                    app_main
 ***************************************************/
extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "ESP KWS + WiFi + UART starting...");

    // WiFi：带回调
    wifi_conn_init(on_wifi_state, NULL);
    wifi_conn_start();

    // I2S / UART / KWS 初始化
    i2s_init();
    uart_init();

    if (!kws_init()) {
        ESP_LOGE(TAG, "kws_init failed, rebooting...");
        esp_restart();
    }

    // 启动 KWS 任务
    xTaskCreate(kws_task, "kws_task", 12 * 1024, NULL, 5, NULL);
    // 启动 UART 接收任务
    xTaskCreate(uart_rx_task, "uart_rx_task", 4 * 1024, NULL, 4, NULL);
}
