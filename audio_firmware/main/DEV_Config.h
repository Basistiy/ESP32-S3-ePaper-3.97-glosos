/*****************************************************************************
* | File        :   DEV_Config.h
* | Author      :   Waveshare team / ESP-IDF adaptation
* | Function    :   Hardware underlying interface for 3.97" e-Paper (ESP-IDF)
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

// e-Paper Display Pin Mapping (Waveshare ESP32-S3 3.97" board)
#define EPD_SCK_PIN   11
#define EPD_MOSI_PIN  12
#define EPD_CS_PIN    10
#define EPD_RST_PIN   46
#define EPD_DC_PIN    9
#define EPD_BUSY_PIN  3
#define EPD_PWR_PIN   -1

#define GPIO_PIN_SET   1
#define GPIO_PIN_RESET 0

#define DEV_Digital_Write(_pin, _value) gpio_set_level((gpio_num_t)(_pin), (_value))
#define DEV_Digital_Read(_pin)          gpio_get_level((gpio_num_t)(_pin))

static inline void DEV_Delay_ms(uint32_t ms)
{
    if (ms >= 10 && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        esp_rom_delay_us(ms * 1000);
    }
}

UBYTE DEV_Module_Init(void);
void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode);
void DEV_SPI_WriteByte(UBYTE data);
UBYTE DEV_SPI_ReadByte(void);
void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len);
void DEV_Module_Exit(void);

#ifdef __cplusplus
}
#endif

#endif // _DEV_CONFIG_H_
