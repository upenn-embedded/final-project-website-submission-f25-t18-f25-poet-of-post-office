#ifndef ST7735_H_
#define ST7735_H_

#include <avr/io.h>

#define LCD_PORT PORTB
#define LCD_DDR  DDRB
#define LCD_DC   PORTB0
#define LCD_RST  PORTB1
#define LCD_TFT_CS PORTB2
#define LCD_MOSI PORTB3
#define LCD_SCK  PORTB5

#define LCD_LITE_PORT PORTD
#define LCD_LITE_DDR  DDRD
#define LCD_LITE      PORTD6

#define LCD_WIDTH 160
#define LCD_HEIGHT 128
#define LCD_SIZE (LCD_WIDTH * LCD_HEIGHT)

#define ST7735_NOP     0x00
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_NORON   0x13
#define ST7735_INVOFF  0x20
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08

#define set(r,b) ((r)|=(1<<(b)))
#define clear(r,b) ((r)&=~(1<<(b)))

void Delay_ms(unsigned int n);
void lcd_init(void);
void sendCommands(const uint8_t *cmds, uint8_t length);
void LCD_setAddr(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void SPI_ControllerTx(uint8_t data);
void SPI_ControllerTx_stream(uint8_t data);
void SPI_ControllerTx_16bit(uint16_t data);
void SPI_ControllerTx_16bit_stream(uint16_t data);
void LCD_brightness(uint8_t intensity);
void LCD_rotate(uint8_t r);

#endif
