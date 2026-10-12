/*****************************************************************************
* | File        :   DEV_Config.c
* | Author      :   Waveshare team / ESP-IDF adaptation
* | Function    :   Hardware underlying interface for 3.97" e-Paper (ESP-IDF)
******************************************************************************/
#include "DEV_Config.h"

static void GPIO_Config(void)
{
    gpio_config_t io_conf = {0};

    // Output pins: CS, DC, RST, SCK, MOSI
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_CS_PIN)  |
                           (1ULL << EPD_DC_PIN)  |
                           (1ULL << EPD_RST_PIN) |
                           (1ULL << EPD_SCK_PIN) |
                           (1ULL << EPD_MOSI_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Input pin: BUSY
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << EPD_BUSY_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)EPD_CS_PIN, 1);
    gpio_set_level((gpio_num_t)EPD_SCK_PIN, 0);
}

void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode)
{
    if (Mode == 0) {
        gpio_set_direction((gpio_num_t)GPIO_Pin, GPIO_MODE_INPUT);
    } else {
        gpio_set_direction((gpio_num_t)GPIO_Pin, GPIO_MODE_OUTPUT);
    }
}

UBYTE DEV_Module_Init(void)
{
    GPIO_Config();
    return 0;
}

void DEV_SPI_WriteByte(UBYTE data)
{
    gpio_set_level((gpio_num_t)EPD_CS_PIN, 0);
    for (int i = 0; i < 8; i++) {
        if ((data & 0x80) == 0) {
            gpio_set_level((gpio_num_t)EPD_MOSI_PIN, 0);
        } else {
            gpio_set_level((gpio_num_t)EPD_MOSI_PIN, 1);
        }
        data <<= 1;
        gpio_set_level((gpio_num_t)EPD_SCK_PIN, 1);
        gpio_set_level((gpio_num_t)EPD_SCK_PIN, 0);
    }
    gpio_set_level((gpio_num_t)EPD_CS_PIN, 1);
}

UBYTE DEV_SPI_ReadByte(void)
{
    UBYTE j = 0xff;
    GPIO_Mode(EPD_MOSI_PIN, 0);
    gpio_set_level((gpio_num_t)EPD_CS_PIN, 0);
    for (int i = 0; i < 8; i++) {
        j = j << 1;
        if (gpio_get_level((gpio_num_t)EPD_MOSI_PIN)) {
            j |= 0x01;
        } else {
            j &= 0xfe;
        }
        gpio_set_level((gpio_num_t)EPD_SCK_PIN, 1);
        gpio_set_level((gpio_num_t)EPD_SCK_PIN, 0);
    }
    gpio_set_level((gpio_num_t)EPD_CS_PIN, 1);
    GPIO_Mode(EPD_MOSI_PIN, 1);
    return j;
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len)
{
    for (UDOUBLE i = 0; i < len; i++) {
        DEV_SPI_WriteByte(pData[i]);
    }
}

void DEV_Module_Exit(void)
{
    gpio_set_level((gpio_num_t)EPD_RST_PIN, 0);
}
