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


#include "wifi_connect.h"
#include "Status_server.h"

static const char* TAG = "APP";

#define I2S_SCK_IO      GPIO_NUM_17
#define I2S_WS_IO       GPIO_NUM_18
#define I2S_SD_IO       GPIO_NUM_16

#define I2S_READ_SAMPLES    512
static i2s_chan_handle_t s_rx_handle = NULL;


#define UART_PORT_NUM   UART_NUM_1

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
        UART_BUF_SIZE * 2,   
        0,                  
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


static void on_wifi_state(wifi_conn_state_t st, void* user)
{
    ESP_LOGI(TAG, "WiFi state -> %d", st);

    switch (st) {
    case WIFI_CONN_STA_OK:

        status_server_start();
        status_set_mode("auto");     
        status_set_voice("none");
        status_set_gesture("unknown");
        break;

    default:
        
        status_server_stop();
        break;
    }
}


static void i2s_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S...");


    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle));

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AF_SAMPLE_RATE);

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO
    );

    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

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

    i2s_std_config_t std_cfg = {
        .clk_cfg  = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

    ESP_LOGI(TAG, "I2S initialized: %d Hz", AF_SAMPLE_RATE);
}


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
    
                    line[line_pos] = '\0';
                    ESP_LOGI(TAG, "UART RX line: %s", line);

                    int  hr = -1;
                    char gest[32] = {0};


                    char* p_hr   = strstr(line, "hr=");
                    char* p_gest = strstr(line, "gest=");
                    if (p_hr) {
                        hr = atoi(p_hr + 3);
                    }
                    if (p_gest) {

                        strncpy(gest, p_gest + 5, sizeof(gest) - 1);

                        for (int k = 0; gest[k]; ++k) {
                            if (gest[k] == ',' || gest[k] == ' ' || gest[k] == '\r')
                                gest[k] = '\0';
                        }
                    }

                    if (hr >= 0) {
                        char mode_str[32];
                        snprintf(mode_str, sizeof(mode_str), "HR:%d", hr);

                        status_set_mode(mode_str);
                    }
                    if (gest[0]) {
                        status_set_gesture(gest);
                    }

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


#define RING_BUFFER_SAMPLES     (AF_SAMPLE_RATE * 2)   // 2s
#define ENERGY_WINDOW_SAMPLES   1600                  // 0.1s
#define ENERGY_THRESHOLD        0.8f
#define VAD_START_CHUNKS        2
#define VAD_END_CHUNKS          4

static void kws_task(void* arg)
{
    ESP_LOGI(TAG, "kws_task started");

    static int16_t ring_buffer[RING_BUFFER_SAMPLES];
    int ring_write = 0;
    bool ring_filled = false;

    int32_t i2s_buf[I2S_READ_SAMPLES];

    static int16_t window[AF_CLIP_SAMPLES];   
    static float   features[AF_FEATURE_SIZE];

    bool in_speech       = false;
    int  vad_high_count  = 0;
    int  vad_low_count   = 0;

    while (1) {

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

        for (size_t i = 0; i < samples; ++i) {
            int32_t raw = i2s_buf[i];
            int16_t s   = (int16_t)(raw >> 13);  

            ring_buffer[ring_write] = s;
            ring_write = (ring_write + 1) % RING_BUFFER_SAMPLES;
            if (ring_write == 0) {
                ring_filled = true;
            }
        }

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


        if (in_speech) {
  
            if (!ring_filled && ring_write < AF_CLIP_SAMPLES) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

 
            int start_idx = (ring_write - AF_CLIP_SAMPLES + RING_BUFFER_SAMPLES) % RING_BUFFER_SAMPLES;
            for (int n = 0; n < AF_CLIP_SAMPLES; ++n) {
                int idx = (start_idx + n) % RING_BUFFER_SAMPLES;
                window[n] = ring_buffer[idx];
            }

            af_compute_log_mel(window, features);

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


                status_set_voice(name);

      
                char tx_buf[64];
                int  n = snprintf(tx_buf, sizeof(tx_buf),
                                  "word=%s\n", name);
                if (n > 0) {
                    uart_write_bytes(UART_PORT_NUM, tx_buf, n);
                }
            }


            vTaskDelay(pdMS_TO_TICKS(200));
        }


        vTaskDelay(pdMS_TO_TICKS(1));
    }
}


extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "ESP KWS + WiFi + UART starting...");

    wifi_conn_init(on_wifi_state, NULL);
    wifi_conn_start();

    i2s_init();
    uart_init();

    if (!kws_init()) {
        ESP_LOGE(TAG, "kws_init failed, rebooting...");
        esp_restart();
    }

    xTaskCreate(kws_task, "kws_task", 12 * 1024, NULL, 5, NULL);

    xTaskCreate(uart_rx_task, "uart_rx_task", 4 * 1024, NULL, 4, NULL);
}
