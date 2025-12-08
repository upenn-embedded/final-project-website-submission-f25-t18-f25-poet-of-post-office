
#include "LCD_GFX.h"
#include "ST7735.h"
#include <math.h>
#include <string.h>


uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
	return ((((31*(red+4))/255)<<11) | (((63*(green+2))/255)<<5) | ((31*(blue+4))/255));
}


void LCD_drawPixel(uint8_t x, uint8_t y, uint16_t color) {
	LCD_setAddr(x,y,x,y);
	SPI_ControllerTx_16bit(color);
}

void LCD_drawCharSize(uint8_t x, uint8_t y,
                      uint16_t character,
                      uint16_t fColor, uint16_t bColor,
                      uint8_t size)
{
    if (size == 0) size = 1;

    uint8_t ch  = (uint8_t)character;
    uint8_t row = ch - 0x20;          // ASCII ?

    // ?????5 ??? * size?8 ??? * size
    if ((LCD_WIDTH  - x < 5 * size) ||
        (LCD_HEIGHT - y < 8 * size)) {
        return;
    }

    for (uint8_t i = 0; i < 5; i++) {         // ???
        uint8_t pixels = ASCII[row][i];

        for (uint8_t j = 0; j < 8; j++) {     // ??? bit
            uint16_t color =
                ((pixels >> j) & 0x01) ? fColor : bColor;

            // ??? size�size ????
            for (uint8_t xx = 0; xx < size; xx++) {
                for (uint8_t yy = 0; yy < size; yy++) {
                    LCD_drawPixel(x + i * size + xx,
                                  y + j * size + yy,
                                  color);
                }
            }
        }
    }
}


 
void LCD_drawChar(uint8_t x, uint8_t y, uint16_t character, uint16_t fColor, uint16_t bColor){
	uint16_t row = character - 0x20;		//Determine row of ASCII table starting at space
	int i, j;
	if ((LCD_WIDTH-x>7)&&(LCD_HEIGHT-y>7)){
		for(i=0;i<5;i++){
			uint8_t pixels = ASCII[row][i]; //Go through the list of pixels
			for(j=0;j<8;j++){
				if ((pixels>>j)&1==1){
					LCD_drawPixel(x+i,y+j,fColor);
				}
				else {
					LCD_drawPixel(x+i,y+j,bColor);
				}
			}
		}
	}
}





void LCD_drawCircle(uint8_t x0, uint8_t y0, uint8_t radius,uint16_t color)
{

	if ((LCD_WIDTH-x0>7)&&(LCD_HEIGHT-y0>7)){
        for(int i = 0; i <= radius; i++){
            int x1 = (int)sqrt(fabs(pow(radius, 2)-pow(i, 2)));
            int x2 = (int)sqrt(fabs(pow(radius, 2)-pow(i, 2)));
            for(int j = x0-x2; j < x0+x1; j++){
                LCD_drawPixel(j,y0+i,color); 
            }
            for(int j = x0-x2; j < x0+x1; j++){
                LCD_drawPixel(j,y0-i,color); 
            }
        }
	}
}

void LCD_drawLine(short x0,short y0,short x1,short y1,uint16_t c)
{
		if(x0 != x1){
        int k = (y0-y1)/(x0-x1);
        int b = y1-k*x1;
        if(x0<x1){
            for(int i=x0; i<=x1; i++ ){
                LCD_drawPixel(i,k*i+b,c);
            }
        }else{
            for(int i=x1; i<=x0; i++ ){
                LCD_drawPixel(i,k*i+b,c);
            }
        }    
    }else{
        if(y0<y1){
            for(int i=y0; i<=y1; i++ ){
                LCD_drawPixel(x0, i,c);
            }
        }else{
            for(int i=y1; i<=y0; i++ ){
                LCD_drawPixel(x0, i,c);
            }  
        }

    }

}

void LCD_drawBlock(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,uint16_t color)
{
    LCD_setAddr(x0, y0, x1, y1);
    clear(LCD_PORT, LCD_TFT_CS);
    for(int i= x0; i<x1+1; i++){
        for(int j = y0; j<y1+1;j++){
               SPI_ControllerTx_16bit(color);     
        }

    }
    set(LCD_PORT, LCD_TFT_CS);
}

void LCD_setScreen(uint16_t color) 
{
    LCD_setAddr(0, 0, 159, 127);
    clear(LCD_PORT, LCD_TFT_CS);
    for(int i=0; i<160; i++){
        for(int j=0; j<128; j++){
            SPI_ControllerTx_16bit(color);
        }
    }
	set(LCD_PORT, LCD_TFT_CS);

}

void LCD_drawStringSize(uint8_t x, uint8_t y,
                        char *str,
                        uint16_t fg, uint16_t bg,
                        uint8_t size)
{
    if (size == 0) size = 1;

    for (uint8_t i = 0; i < strlen(str); i++) {
        // ???????? 5*size??? 1*size ????
        uint8_t x_char = x + i * (5 * size + 1 * size);
        LCD_drawCharSize(x_char, y, (uint8_t)str[i], fg, bg, size);
    }
}

void LCD_drawString(uint8_t x, uint8_t y,
                    char* str, uint16_t fg, uint16_t bg)
{
    LCD_drawStringSize(x, y, str, fg, bg, 5);
}