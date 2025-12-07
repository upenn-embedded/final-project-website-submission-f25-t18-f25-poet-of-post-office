#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include "ST7735.h"
#include "LCD_GFX.h"

#define NUM_LEDS       150
#define NEOPIXEL_PORT  PORTD
#define NEOPIXEL_DDR   DDRD
#define NEOPIXEL_PIN   PD5

#define RING_NUM_LEDS  24
#define RING_PORT      PORTD
#define RING_DDR       DDRD
#define RING_PIN       PD1

#define FLEX_ADC_CHANNEL   1
#define FLEX_THRESHOLD     300

void adc_init(void)
{
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN)  |
             (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    DIDR0 |= (1 << ADC1D);
}

uint16_t adc_read(uint8_t channel)
{
    channel &= 0x07;
    ADMUX = (ADMUX & 0xF0) | channel;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) {}
    return ADC;
}

uint16_t flex_get_value(void)
{
    const uint8_t samples = 4;
    uint32_t sum = 0;

    for (uint8_t i = 0; i < samples; i++) {
        sum += adc_read(FLEX_ADC_CHANNEL);
        _delay_ms(2);
    }
    return (uint16_t)(sum / samples);
}

static inline void ws2812_send_bit(uint8_t bitVal)
{
    if (bitVal) {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(NEOPIXEL_PORT)),
              [bit]  "I" (NEOPIXEL_PIN)
        );
    } else {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(NEOPIXEL_PORT)),
              [bit]  "I" (NEOPIXEL_PIN)
        );
    }
}

static void ws2812_send_byte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        ws2812_send_bit(byte & 0x80);
        byte <<= 1;
    }
}

static void ws2812_send_color(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_send_byte(g);
    ws2812_send_byte(r);
    ws2812_send_byte(b);
}

static void ws2812_show_color(uint16_t count,
                              uint8_t r, uint8_t g, uint8_t b)
{
    cli();
    for (uint16_t i = 0; i < count; i++) {
        ws2812_send_color(r, g, b);
    }
    sei();
    _delay_us(60);
}

static inline void ring_ws2812_send_bit(uint8_t bitVal)
{
    if (bitVal) {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(RING_PORT)),
              [bit]  "I" (RING_PIN)
        );
    } else {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            "nop \n\t""nop \n\t""nop \n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(RING_PORT)),
              [bit]  "I" (RING_PIN)
        );
    }
}

static void ring_ws2812_send_byte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        ring_ws2812_send_bit(byte & 0x80);
        byte <<= 1;
    }
}

static void ring_ws2812_send_color(uint8_t r, uint8_t g, uint8_t b)
{
    ring_ws2812_send_byte(g);
    ring_ws2812_send_byte(r);
    ring_ws2812_send_byte(b);
}

static void ring_ws2812_show_color(uint16_t count,
                                   uint8_t r, uint8_t g, uint8_t b)
{
    cli();
    for (uint16_t i = 0; i < count; i++) {
        ring_ws2812_send_color(r, g, b);
    }
    sei();
    _delay_us(60);
}

static void ring_on(void)
{
    ring_ws2812_show_color(RING_NUM_LEDS, 50, 50, 50);
}

static void ring_off(void)
{
    ring_ws2812_show_color(RING_NUM_LEDS, 0, 0, 0);
}

void strip_on(void)
{
    ring_off();
    ws2812_show_color(NUM_LEDS, 50, 50, 50);
    LCD_drawString(10, 40, "BIST", WHITE, BLACK);
}

void strip_off(void)
{
    ws2812_show_color(NUM_LEDS, 0, 0, 0);
    ring_on();
    LCD_drawString(10, 40, "PALM", WHITE, BLACK);
}

void Initialize()
{
    lcd_init();
    LCD_setScreen(rgb565(0, 0, 0));
}

int main(void)
{
    Initialize();

    NEOPIXEL_DDR  |= (1 << NEOPIXEL_PIN);
    NEOPIXEL_PORT &= ~(1 << NEOPIXEL_PIN);

    RING_DDR  |= (1 << RING_PIN);
    RING_PORT &= ~(1 << RING_PIN);

    adc_init();

    uint8_t last_state = 0xFF;

    while (1) {
        uint16_t flex_val = flex_get_value();
        uint8_t cur_state = (flex_val > FLEX_THRESHOLD) ? 1 : 0;

        if (cur_state != last_state) {
            if (cur_state == 1) {
                strip_off();
            } else {
                strip_on();
            }
            last_state = cur_state;
        }

        _delay_ms(20);
    }
}
