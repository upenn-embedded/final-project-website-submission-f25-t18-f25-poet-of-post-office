#include "ui.h"

#include <string.h>
#include <avr/io.h>
#include "ST7735.h"
#include "LCD_GFX.h"


extern void LCD_drawStringSize(uint8_t x, uint8_t y,
                               char *str,
                               uint16_t fg, uint16_t bg,
                               uint8_t size);


static gesture_t g_current_gesture      = GESTURE_PALM;
static uint16_t  g_last_bpm_displayed   = 0xFFFF;  
static uint8_t   g_heart_big            = 0;   


static void ui_uint_to_str(uint16_t v, char *buf)
{
    char tmp[6];
    uint8_t i = 0;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (v > 0 && i < sizeof(tmp)) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }

    uint8_t j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}



#define HEART_W 12
#define HEART_H 10

static const uint16_t heart_bitmap[HEART_H] = {
    0b000011001100,
    0b000111111110,
    0b001111111111,
    0b001111111111,
    0b001111111111,
    0b000111111110,
    0b000011111100,
    0b000001111000,
    0b000000110000,
    0b000000100000
};


static void ui_draw_heart(uint8_t x, uint8_t y,
                          uint8_t scale,
                          uint16_t fg, uint16_t bg)
{
    if (scale == 0) scale = 1;

    for (uint8_t row = 0; row < HEART_H; row++) {
        uint16_t line = heart_bitmap[row];

        for (uint8_t col = 0; col < HEART_W; col++) {
            uint8_t bit   = (line >> (HEART_W - 1 - col)) & 0x01;
            uint16_t color = bit ? fg : bg;

            for (uint8_t sx = 0; sx < scale; sx++) {
                for (uint8_t sy = 0; sy < scale; sy++) {
                    LCD_drawPixel(x + col * scale + sx,
                                  y + row * scale + sy,
                                  color);
                }
            }
        }
    }
}


static void ui_draw_hr_area(void)
{
   
    LCD_drawBlock(0, 0, LCD_WIDTH - 1, 23, BLACK);

    uint8_t scale = g_heart_big ? 2 : 1;

    ui_draw_heart(2, 2, scale, RED, BLACK);

    char bpm_text[16];

    if (g_last_bpm_displayed == 0 || g_last_bpm_displayed == 0xFFFF) {
        
        strcpy(bpm_text, "-- bpm");
    } else {
        char num[8];
        ui_uint_to_str(g_last_bpm_displayed, num);
        strcpy(bpm_text, num);
        strcat(bpm_text, " bpm");
    }

    LCD_drawStringSize(30, 6, bpm_text, WHITE, BLACK, 2);
}


void UI_OnHeartRateUpdated(uint16_t bpm)
{
    g_last_bpm_displayed = bpm;
    ui_draw_hr_area();
}

void UI_OnGestureUpdated(gesture_t gesture)
{
    g_current_gesture = gesture;

    LCD_drawBlock(0, 40, LCD_WIDTH - 1, 95, BLACK);

    char text[16];

    switch (gesture) {
        case GESTURE_0:
            strcpy(text, "PALM");
            break;
        case GESTURE_7:
            strcpy(text, "FIST");
            break;
        case GESTURE_1:
            strcpy(text, "ROCK");
            break;
        case GESTURE_2:
            strcpy(text, "OK");
            break;
        case GESTURE_3:
            strcpy(text, "TAUNT");
            break;
        case GESTURE_4:
            strcpy(text, "VICTORY");
            break;
        case GESTURE_5:
            strcpy(text, "POINTING");
            break;
        case GESTURE_6:
            strcpy(text, "FXXK");
            break;
        default:
            strcpy(text, "G?");
            break;
    }

    uint8_t size    = 3;
    uint8_t char_w  = 5 * size + 1 * size;
    uint8_t len     = (uint8_t)strlen(text);
    uint8_t text_w  = (uint8_t)(len * char_w);
    uint8_t text_h  = (uint8_t)(8 * size);

    uint8_t x = (LCD_WIDTH  - text_w) / 2;
    uint8_t y = (LCD_HEIGHT - text_h) / 2;

    LCD_drawStringSize(x, y, text, WHITE, BLACK, size);
}


void UI_HeartbeatTick(void)
{
    g_heart_big = !g_heart_big;
    ui_draw_hr_area();           
}

void UI_Init(void)
{

    LCD_setScreen(rgb565(0, 0, 0));

    g_last_bpm_displayed = 0;
    g_heart_big          = 0;
    ui_draw_hr_area();

    UI_OnGestureUpdated(GESTURE_0);
}
