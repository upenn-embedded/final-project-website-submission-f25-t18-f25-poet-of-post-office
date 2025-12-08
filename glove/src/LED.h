#ifndef LED_H
#define LED_H

#include <avr/io.h>
#include <stdint.h>

#define NUM_LEDS       150
#define RING_NUM_LEDS  24

void LED_Init(void);

void ws2812_show_color(uint16_t count, uint8_t r, uint8_t g, uint8_t b);

void ring_ws2812_show_color(uint16_t count, uint8_t r, uint8_t g, uint8_t b);

#endif /* LED_H */
