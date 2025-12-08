#include "LED.h"
#include <util/delay.h>
#include <avr/interrupt.h>

#define NEOPIXEL_PORT  PORTD
#define NEOPIXEL_DDR   DDRD
#define NEOPIXEL_PIN   PD5

#define RING_PORT      PORTD
#define RING_DDR       DDRD
#define RING_PIN       PD2


void LED_Init(void)
{
 
    NEOPIXEL_DDR  |= (1 << NEOPIXEL_PIN);
    NEOPIXEL_PORT &= ~(1 << NEOPIXEL_PIN);


    RING_DDR  |= (1 << RING_PIN);
    RING_PORT &= ~(1 << RING_PIN);
}


static inline void ws2812_send_bit(uint8_t bitVal)
{
    if (bitVal) {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t""nop\n\t""nop\n\t"
            "nop\n\t""nop\n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(NEOPIXEL_PORT)),
              [bit]  "I" (NEOPIXEL_PIN)
        );
    } else {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t""nop\n\t""nop\n\t"
            "nop\n\t""nop\n\t""nop\n\t"
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


void ws2812_show_color(uint16_t count,
                       uint8_t r, uint8_t g, uint8_t b)
{
    cli();
    for (uint16_t i = 0; i < count; i++) {
        ws2812_send_color(r, g, b);
    }
    sei();
    _delay_us(60);   /* reset */
}



static inline void ring_ws2812_send_bit(uint8_t bitVal)
{
    if (bitVal) {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t""nop\n\t""nop\n\t"
            "nop\n\t""nop\n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t"
            :
            : [port] "I" (_SFR_IO_ADDR(RING_PORT)),
              [bit]  "I" (RING_PIN)
        );
    } else {
        asm volatile (
            "sbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t"
            "cbi  %[port], %[bit] \n\t"
            "nop\n\t""nop\n\t""nop\n\t""nop\n\t""nop\n\t"
            "nop\n\t""nop\n\t""nop\n\t"
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


void ring_ws2812_show_color(uint16_t count,
                            uint8_t r, uint8_t g, uint8_t b)
{
    cli();
    for (uint16_t i = 0; i < count; i++) {
        ring_ws2812_send_color(r, g, b);
    }
    sei();
    _delay_us(60);   /* reset */
}
