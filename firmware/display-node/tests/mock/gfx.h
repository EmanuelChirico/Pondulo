#ifndef MOCK_GFX_H
#define MOCK_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int16_t gCoord;
typedef uint16_t gColor;
typedef uint16_t color_t;

#define Black   0x0000
#define White   0xFFFF
#define Yellow  0xFFE0
#define HTML2COLOR(c) ((color_t)(((c & 0xF80000) >> 8) | ((c & 0x00FC00) >> 5) | ((c & 0x0000F8) >> 3)))

typedef void* font_t;
typedef enum { justifyLeft, justifyCenter, justifyRight } gJustify;

gCoord gdispGetWidth(void);
gCoord gdispGetHeight(void);
font_t gdispOpenFont(const char *name);

void gdispSetClip(gCoord x, gCoord y, gCoord cx, gCoord cy);
void gdispUnsetClip(void);

void gdispFillArea(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color);
void gdispDrawBox(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color);
void gdispDrawStringBox(gCoord x, gCoord y, gCoord cx, gCoord cy, const char *str, font_t font, gColor color, gJustify justify);
void gdispFillCircle(gCoord x, gCoord y, gCoord radius, gColor color);
void gdispDrawCircle(gCoord x, gCoord y, gCoord radius, gColor color);
void gdispDrawLine(gCoord x0, gCoord y0, gCoord x1, gCoord y1, gColor color);
void gdispDrawPixel(gCoord x, gCoord y, gColor color);
void gdispBlitArea(gCoord x, gCoord y, gCoord cx, gCoord cy, const gColor *buffer);

#endif /* MOCK_GFX_H */
