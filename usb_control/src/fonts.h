#ifndef __FONTS_H
#define __FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_HEIGHT_FONT         41
#define MAX_WIDTH_FONT          32
#define OFFSET_BITMAP           54

// ASCII Font structure
typedef struct _tFont {
    const uint8_t *table;
    uint16_t Width;
    uint16_t Height;
} sFONT;

// GB2312 Font structure (required by GUI_Paint header signatures)
typedef struct {
    unsigned char index[4];
    const unsigned char matrix[MAX_HEIGHT_FONT * MAX_WIDTH_FONT / 8 + 1];
} CH_CN;

typedef struct {
    const CH_CN *table;
    uint16_t size;
    uint16_t ASCII_Width;
    uint16_t Width;
    uint16_t Height;
} cFONT;

// Sole high-contrast bitmap font used throughout the UI
extern sFONT Font24;

#ifdef __cplusplus
}
#endif

#endif /* __FONTS_H */
