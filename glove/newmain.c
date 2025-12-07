#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>

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

/* ================= UART ================= */

static void uart_init(void)
{
    uint16_t ubrr = 103;
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);

    UCSR0B = (1 << TXEN0);
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

static void uart_put_uint(uint16_t v)
{
    char buf[6];
    uint8_t i = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0 && i < sizeof(buf)) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i > 0) uart_putc(buf[--i]);
}

/* ================= Flex ??? ADC ?? ================= */

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

/* ================= ?????????? strip_on/off + ring_on/off? ================= */

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

/* ?? 3bit ???? 0~7 ???? */
static void ApplyGestureLED(uint8_t gesture)
{
    switch (gesture) {
        case 0: /* 000: ????? PALM */
            strip_off();
            break;

        case 1: /* 001 */
            ring_off();
            ws2812_show_color(NUM_LEDS, 50, 0, 0);   /* ?? */
            break;

        case 2: /* 010 */
            ring_off();
            ws2812_show_color(NUM_LEDS, 0, 50, 0);   /* ?? */
            break;

        case 3: /* 011 */
            ring_off();
            ws2812_show_color(NUM_LEDS, 0, 0, 50);   /* ?? */
            break;

        case 4: /* 100 */
            ring_on();
            ws2812_show_color(NUM_LEDS, 50, 50, 0);  /* ? */
            break;

        case 5: /* 101 */
            ring_on();
            ws2812_show_color(NUM_LEDS, 50, 0, 50);  /* ?? */
            break;

        case 6: /* 110 */
            ring_on();
            ws2812_show_color(NUM_LEDS, 0, 50, 50);  /* ? */
            break;

        case 7: /* 111: ????? FIST */
        default:
            strip_on();
            break;
    }
}

/* ================= main ================= */

int main(void)
{
    uart_init();
    LED_Init();

    lcd_init();
    UI_Init();

    PulseSensor_Init(0);   /* ?????? ADC0 */

    uint8_t last_gesture = 0xFF;

    while (1) {

        /* ---------- ?????????? UI ---------- */
        if (PulseSensor_IsBeat()) {
            uint16_t bpm = PulseSensor_GetBPM();
            uart_puts("Beat! BPM = ");
            uart_put_uint(bpm);
            uart_puts("\r\n");

            UI_OnHeartRateUpdated(bpm);
        }

        /* ---------- ?? flex??? 3bit ???? ---------- */
        uint16_t f1 = flex_get_value(FLEX1_ADC_CHANNEL);
        uint16_t f2 = flex_get_value(FLEX2_ADC_CHANNEL);
        uint16_t f3 = flex_get_value(FLEX3_ADC_CHANNEL);

        uint8_t s1 = (f1 > FLEX1_THRESHOLD) ? 0 : 1;
        uint8_t s2 = (f2 > FLEX2_THRESHOLD) ? 1 : 0;
        uint8_t s3 = (f3 > FLEX3_THRESHOLD) ? 1 : 0;

        uint8_t gesture = (s3 << 2) | (s2 << 1) | s1;  /* 0~7 */

        if (gesture != last_gesture) {
            ApplyGestureLED(gesture);
            UI_OnGestureUpdated(gesture);   /* UI ??? 0~7 ??????/?? */
            last_gesture = gesture;
        }

        _delay_ms(20);
    }
}
