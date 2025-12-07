#ifndef LED_H
#define LED_H

#include <avr/io.h>
#include <stdint.h>

/* 灯条和灯环的 LED 数量 */
#define NUM_LEDS       150
#define RING_NUM_LEDS  24

/* 初始化 WS2812 相关 IO 方向、默认电平 */
void LED_Init(void);

/* 主灯条：一次性把所有 LED 设置为同一个颜色 */
void ws2812_show_color(uint16_t count, uint8_t r, uint8_t g, uint8_t b);

/* 灯环：一次性把所有 LED 设置为同一个颜色 */
void ring_ws2812_show_color(uint16_t count, uint8_t r, uint8_t g, uint8_t b);

#endif /* LED_H */
