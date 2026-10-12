/*****************************************************************************
* | File        :   EPD_3IN97.h
* | Author      :   Waveshare team
* | Function    :   3.97inch e-paper display driver
******************************************************************************/
#ifndef __EPD_3IN97_H_
#define __EPD_3IN97_H_

#include "DEV_Config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display resolution
#define EPD_3IN97_WIDTH       800
#define EPD_3IN97_HEIGHT      480

void EPD_3IN97_Init(void);
void EPD_3IN97_Init_Fast(void);
void EPD_3IN97_Init_Partial(void);
void EPD_3IN97_Init_4GRAY(void);
void EPD_3IN97_Clear(void);
void EPD_3IN97_Clear_Black(void);
void EPD_3IN97_Display(const UBYTE *Image);
void EPD_3IN97_Display_Base(const UBYTE *Image);
void EPD_3IN97_Display_Fast(const UBYTE *Image);
void EPD_3IN97_Display_Fast_Base(const UBYTE *Image);
void EPD_3IN97_V2_Display_Window(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh);
void EPD_3IN97_V2_Display_Window_Base(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh);
void EPD_3IN97_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend);
void EPD_3IN97_Display_4Gray(const UBYTE *Image);
void EPD_3IN97_Sleep(void);

#ifdef __cplusplus
}
#endif

#endif // __EPD_3IN97_H_
