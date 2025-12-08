#include <avr/io.h>
#include <util/delay.h>
#include "ST7735.h"

static void lcd_pin_init(void)
{
    LCD_DDR |= (1<<LCD_DC)|(1<<LCD_RST)|(1<<LCD_TFT_CS)|(1<<LCD_MOSI)|(1<<LCD_SCK);
    LCD_LITE_DDR |= (1<<LCD_LITE);

    TCCR0A |= (1<<COM0A1)|(1<<WGM01)|(1<<WGM00);
    TCCR0B |= (1<<CS02);
    OCR0A = 127;

    _delay_ms(50);
    set(LCD_PORT, LCD_RST);
}

static void SPI_init(void)
{
    SPCR0 = (1<<SPE) | (1<<MSTR);
    SPSR0 = (1<<SPI2X);
}

void Delay_ms(unsigned int n)
{
    while(n--) _delay_ms(1);
}

void SPI_ControllerTx_stream(uint8_t value)
{
    SPDR0 = value;
    while(!(SPSR0 & (1<<SPIF)));
}

void SPI_ControllerTx(uint8_t value)
{
    clear(LCD_PORT, LCD_TFT_CS);
    SPI_ControllerTx_stream(value);
    set(LCD_PORT, LCD_TFT_CS);
}

void SPI_ControllerTx_16bit(uint16_t value)
{
    uint8_t highByte = value >> 8;
    clear(LCD_PORT, LCD_TFT_CS);

    SPDR0 = highByte;
    while(!(SPSR0 & (1<<SPIF)));

    SPDR0 = value;
    while(!(SPSR0 & (1<<SPIF)));

    set(LCD_PORT, LCD_TFT_CS);
}

void SPI_ControllerTx_16bit_stream(uint16_t value)
{
    uint8_t highByte = value >> 8;

    SPDR0 = highByte;
    while(!(SPSR0 & (1<<SPIF)));

    SPDR0 = value;
    while(!(SPSR0 & (1<<SPIF)));
}

void sendCommands(const uint8_t *cmdList, uint8_t cmdCount)
{
    uint8_t dataCount;
    uint8_t delayMs;

    clear(LCD_PORT, LCD_TFT_CS);

    while(cmdCount--)
    {
        clear(LCD_PORT, LCD_DC);
        SPI_ControllerTx_stream(*cmdList++);

        dataCount = *cmdList++;

        set(LCD_PORT, LCD_DC);
        while(dataCount--) SPI_ControllerTx_stream(*cmdList++);

        delayMs = *cmdList++;
        if(delayMs) Delay_ms(delayMs == 255 ? 500 : delayMs);
    }

    set(LCD_PORT, LCD_TFT_CS);
}

void lcd_init(void)
{
    lcd_pin_init();
    SPI_init();
    _delay_ms(5);

    static uint8_t initSequence[] =
    {
        ST7735_SWRESET,0,150,
        ST7735_SLPOUT,0,255,
        ST7735_FRMCTR1,3,0x01,0x2C,0x2D,0,
        ST7735_FRMCTR2,3,0x01,0x2C,0x2D,0,
        ST7735_FRMCTR3,6,0x01,0x2C,0x2D,0x01,0x2C,0x2D,0,
        ST7735_INVCTR,1,0x07,0,
        ST7735_PWCTR1,3,0x0A,0x02,0x84,5,
        ST7735_PWCTR2,1,0xC5,5,
        ST7735_PWCTR3,2,0x0A,0x00,5,
        ST7735_PWCTR4,2,0x8A,0x2A,5,
        ST7735_PWCTR5,2,0x8A,0xEE,5,
        ST7735_VMCTR1,1,0x0E,0,
        ST7735_INVOFF,0,0,
        ST7735_MADCTL,1,0xC8,0,
        ST7735_COLMOD,1,0x05,0,
        ST7735_CASET,4,0x00,0x00,0x00,0x7F,0,
        ST7735_RASET,4,0x00,0x00,0x00,0x9F,0,
        ST7735_GMCTRP1,16,0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10,0,
        ST7735_GMCTRN1,16,0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10,0,
        ST7735_NORON,0,10,
        ST7735_DISPON,0,100,
        ST7735_MADCTL,1, MADCTL_MX|MADCTL_MV|MADCTL_RGB,10
    };

    sendCommands(initSequence, 22);
}

void LCD_setAddr(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t addrCommands[] =
    {
        ST7735_CASET,4,0x00,x0,0x00,x1,0,
        ST7735_RASET,4,0x00,y0,0x00,y1,0,
        ST7735_RAMWR,0,0
    };
    sendCommands(addrCommands,3);
}

void LCD_brightness(uint8_t value)
{
    OCR0A = value;
}

void LCD_rotate(uint8_t rotation)
{
    uint8_t madctlValue;
    rotation &= 3;

    if(rotation == 0) madctlValue = MADCTL_MX | MADCTL_MY | MADCTL_RGB;
    else if(rotation == 1) madctlValue = MADCTL_MY | MADCTL_MV | MADCTL_RGB;
    else if(rotation == 2) madctlValue = MADCTL_RGB;
    else madctlValue = MADCTL_MX | MADCTL_MV | MADCTL_RGB;

    uint8_t rotateCmds[] = { ST7735_MADCTL,1,madctlValue,0 };
    sendCommands(rotateCmds,1);
}
