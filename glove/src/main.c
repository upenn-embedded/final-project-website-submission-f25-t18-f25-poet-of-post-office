#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pulse_sensor.h"
#include "ST7735.h"
#include "LCD_GFX.h"
#include "LED.h"
#include "ui.h"

#define FLEX1_ADC_CHANNEL   1
#define FLEX2_ADC_CHANNEL   2
#define FLEX3_ADC_CHANNEL   3

#define FLEX1_THRESHOLD     300
#define FLEX2_THRESHOLD     300
#define FLEX3_THRESHOLD     300

static void uart_init(void)
{
    uint16_t ubrr = 103;              // 115200 @16MHz
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);


    UCSR0B = (1 << TXEN0) | (1 << RXEN0);

    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}


static const char* gesture_to_name(uint8_t gesture)
{
    if (gesture == 0) {
        return "palm";
    } else if (gesture == 7) {
        return "fist";
    } else if (gesture == 1) {
        return "ROCK";
    } else if (gesture == 2) {
        return "OK";
    } else if (gesture == 3) {
        return "TAUNT";
    } else if (gesture == 4) {
        return "Victory";
    } else if (gesture == 5) {
        return "POINTING";
    } else if (gesture == 6) {
        return "FXXK";
    } else {
        return "unknown";
    }
}

static void send_status_to_esp(uint16_t bpm, uint8_t gesture)
{
    char buf[40];
    const char* gname = gesture_to_name(gesture);

    int n = snprintf(buf, sizeof(buf), "hr=%u,gest=%s\n", bpm, gname);
    if (n > 0) {
        uart_puts(buf);
    }
}



#define UART_RX_BUF_SIZE 64
static char    g_uart_rx_buf[UART_RX_BUF_SIZE];
static uint8_t g_uart_rx_pos = 0;

static char g_last_word[16] = "none";

static void OnWordReceived(const char* word)
{
    strncpy(g_last_word, word, sizeof(g_last_word) - 1);
    g_last_word[sizeof(g_last_word) - 1] = '\0';
}

static void uart_handle_line(char* line)
{
    char* p = strstr(line, "word=");
    if (!p) return;

    p += 5; 
    char word[16] = {0};
    uint8_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < sizeof(word) - 1) {
        word[i++] = *p++;
    }
    word[i] = '\0';

    if (i > 0) {
        OnWordReceived(word);
    }
}

static void uart_rx_poll(void)
{
    while (UCSR0A & (1 << RXC0)) {
        char c = UDR0;

        if (c == '\n' || c == '\r') {
            if (g_uart_rx_pos > 0) {
                g_uart_rx_buf[g_uart_rx_pos] = '\0';
                uart_handle_line(g_uart_rx_buf);
                g_uart_rx_pos = 0;
            }
        } else {
            if (g_uart_rx_pos < UART_RX_BUF_SIZE - 1) {
                g_uart_rx_buf[g_uart_rx_pos++] = c;
            } else {
                g_uart_rx_pos = 0;
            }
        }
    }
}



uint16_t flex_get_value(uint8_t adc_channel)
{
    const uint8_t samples = 4;
    uint32_t sum = 0;

    uint8_t admux_backup = ADMUX;

    for (uint8_t i = 0; i < samples; i++) {
        ADMUX = (ADMUX & 0xF0) | (adc_channel & 0x0F);
        ADCSRA |= (1 << ADSC);
        while (ADCSRA & (1 << ADSC));
        sum += ADC;
        _delay_ms(2);
    }

    ADMUX = admux_backup;
    return (uint16_t)(sum / samples);
}


static void ring_on(void)
{
    ring_ws2812_show_color(RING_NUM_LEDS, 50, 50, 50);
}

static void ring_off(void)
{
    ring_ws2812_show_color(RING_NUM_LEDS, 0, 0, 0);
}

static void strip_on(void)
{
    ring_off();
    ws2812_show_color(NUM_LEDS, 50, 50, 50);
}

static void strip_off(void)
{
    ws2812_show_color(NUM_LEDS, 0, 0, 0);
    ring_on();
}


static void ApplyGestureLED(uint8_t gesture)
{
    switch (gesture) {
        case 0: /* PALM */
            strip_off();
            break;

        case 1:
            ring_off();
            ws2812_show_color(NUM_LEDS, 50, 0, 0);
            break;

        case 2:
            ring_off();
            ws2812_show_color(NUM_LEDS, 0, 50, 0);
            break;

        case 3:
            ring_off();
            ws2812_show_color(NUM_LEDS, 0, 0, 50);
            break;

        case 4:
            ring_on();
            ws2812_show_color(NUM_LEDS, 50, 50, 0);
            break;

        case 5:
            ring_on();
            ws2812_show_color(NUM_LEDS, 50, 0, 50);
            break;

        case 6:
            ring_on();
            ws2812_show_color(NUM_LEDS, 0, 50, 50);
            break;

        case 7: /* FIST */
        default:
            strip_on();
            break;
    }
}


int main(void)
{
    uart_init();
    LED_Init();

    lcd_init();
    UI_Init();

    PulseSensor_Init(4);   

    uint8_t  last_gesture   = 0xFF;
    uint16_t last_bpm       = 0;
    uint8_t  bpm_valid      = 0;

    uint16_t heart_anim_ms  = 0;

    while (1) {

        if (PulseSensor_IsBeat()) {
            uint16_t bpm = PulseSensor_GetBPM();

            if (bpm >= 40 && bpm <= 170) {
                last_bpm  = bpm;
                bpm_valid = 1;
            } else {

                bpm = 0;
            }

            UI_OnHeartRateUpdated(bpm);


            if (bpm_valid && last_gesture != 0xFF) {
                send_status_to_esp(last_bpm, last_gesture);
            }
        }


        uint16_t f1 = flex_get_value(FLEX1_ADC_CHANNEL);
        uint16_t f2 = flex_get_value(FLEX2_ADC_CHANNEL);
        uint16_t f3 = flex_get_value(FLEX3_ADC_CHANNEL);

        uint8_t s1 = (f1 > FLEX1_THRESHOLD) ? 0 : 1;
        uint8_t s2 = (f2 > FLEX2_THRESHOLD) ? 1 : 0;
        uint8_t s3 = (f3 > FLEX3_THRESHOLD) ? 1 : 0;

        uint8_t gesture = (s3 << 2) | (s2 << 1) | s1;  /* 0~7 */

        if (gesture != last_gesture) {
            ApplyGestureLED(gesture);
            UI_OnGestureUpdated(gesture);
            last_gesture = gesture;

            if (bpm_valid) {
                send_status_to_esp(last_bpm, last_gesture);
            }
        }


        uart_rx_poll();

        heart_anim_ms += 20;
        if (heart_anim_ms >= 200) {
            heart_anim_ms = 0;
            UI_HeartbeatTick();
        }

        _delay_ms(20);
    }
}
