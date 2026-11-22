#include "ch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gfx.h"
#include "ui_macelleria.h"

systime_t mock_system_time = 1000;

#define SCREEN_W 320
#define SCREEN_H 240

static uint16_t screen[SCREEN_H][SCREEN_W];
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = SCREEN_W, clip_y1 = SCREEN_H;

gCoord gdispGetWidth(void) { return SCREEN_W; }
gCoord gdispGetHeight(void) { return SCREEN_H; }
font_t gdispOpenFont(const char *name) { (void)name; return (font_t)1; }

void gdispSetClip(gCoord x, gCoord y, gCoord cx, gCoord cy) {
  clip_x0 = x < 0 ? 0 : x;
  clip_y0 = y < 0 ? 0 : y;
  clip_x1 = (x + cx) > SCREEN_W ? SCREEN_W : (x + cx);
  clip_y1 = (y + cy) > SCREEN_H ? SCREEN_H : (y + cy);
}

void gdispUnsetClip(void) {
  clip_x0 = 0;
  clip_y0 = 0;
  clip_x1 = SCREEN_W;
  clip_y1 = SCREEN_H;
}

static void draw_pixel(int x, int y, uint16_t color) {
  if (x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
      screen[y][x] = color;
    }
  }
}

void gdispFillArea(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color) {
  for (int j = y; j < y + cy; j++) {
    for (int i = x; i < x + cx; i++) {
      draw_pixel(i, j, color);
    }
  }
}

void gdispDrawBox(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color) {
  for (int i = x; i < x + cx; i++) {
    draw_pixel(i, y, color);
    draw_pixel(i, y + cy - 1, color);
  }
  for (int j = y; j < y + cy; j++) {
    draw_pixel(x, j, color);
    draw_pixel(x + cx - 1, j, color);
  }
}

void gdispDrawStringBox(gCoord x, gCoord y, gCoord cx, gCoord cy, const char *str, font_t font, gColor color, gJustify justify) {
  (void)x; (void)y; (void)cx; (void)cy; (void)str; (void)font; (void)color; (void)justify;
}

void gdispFillCircle(gCoord x, gCoord y, gCoord radius, gColor color) {
  for (int j = y - radius; j <= y + radius; j++) {
    for (int i = x - radius; i <= x + radius; i++) {
      if ((i - x)*(i - x) + (j - y)*(j - y) <= radius * radius) {
        draw_pixel(i, j, color);
      }
    }
  }
}

void gdispDrawCircle(gCoord x, gCoord y, gCoord radius, gColor color) {
  (void)x; (void)y; (void)radius; (void)color;
}

void gdispDrawPixel(gCoord x, gCoord y, gColor color) {
  draw_pixel(x, y, color);
}

void gdispBlitArea(gCoord x, gCoord y, gCoord cx, gCoord cy, const gColor *buffer) {
  for (gCoord j = 0; j < cy; j++) {
    for (gCoord i = 0; i < cx; i++) {
      draw_pixel(x + i, y + j, buffer[j * cx + i]);
    }
  }
}

void gdispDrawLine(gCoord x0, gCoord y0, gCoord x1, gCoord y1, gColor color) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}

void mp3_play_track(uint16_t track) {
  (void)track;
}

static void test_scenario_gesture_nav_conflict(void) {
  printf("--- TEST SCENARIO: GESTURE NAV ACTIVE CONFLICT ---\n");
  uiMacelleriaInit();

  uint16_t border_norm = HTML2COLOR(0x475569);
  uint16_t border_high = Yellow;

  /* Simulate that a swipe gesture happened earlier (e.g. from moving hand or testing),
   * leaving home_gesture_nav_active = true and home_highlight = 0 */
  uiMacelleriaHandleGesture(GESTURE_SWIPE_RIGHT);
  uiMacelleriaHandleGesture(GESTURE_SWIPE_RIGHT);
  uiMacelleriaHandleGesture(GESTURE_SWIPE_RIGHT); /* 3 swipes -> home_highlight = 1 */

  /* 1. Hover on Region 0 (CLINICO) at (80, 80) */
  for (int i = 0; i < 20; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 80, 80);
  }
  printf("Dwell running on Region 0: Region 0 border = 0x%04X\n", screen[50][10]);

  /* 2. User moves hand to Region 1 at (240, 180), passing through gap (160, 143) */
  printf("Moving pointer through gap (160, 143) to Region 1 (240, 180)...\n");
  for (int i = 0; i < 10; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 160, 143); /* In the gap: hit = -1 */
  }
  printf("  In the gap: Region 0 border = 0x%04X (expected norm 0x%04X)\n",
         screen[50][10], border_norm);

  for (int i = 0; i < 20; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 240, 180); /* In Region 1: hit = 1 */
  }
  printf("  In Region 1: Region 0 border = 0x%04X (expected norm 0x%04X), Region 1 border = 0x%04X (expected high 0x%04X)\n",
         screen[50][10], border_norm, screen[149][10], border_high);
  assert(screen[50][10] == border_norm);
  assert(screen[149][10] == border_high);
}

static void test_scenario_pure_hover(void) {
  printf("--- TEST SCENARIO: PURE HOVER SWITCH FROM REGION 0 TO REGION 1 ---\n");
  uiMacelleriaInit();

  uint16_t border_norm = HTML2COLOR(0x475569);
  uint16_t border_high = Yellow;

  /* 1. Hover on Region 0 (CLINICO) at (80, 80) */
  for (int i = 0; i < 20; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 80, 80);
  }
  printf("Dwell running on Region 0:\n");
  printf("  Top-left border (10, 50) = 0x%04X (expected high 0x%04X)\n", screen[50][10], border_high);
  printf("  Bottom border (10, 130)  = 0x%04X (expected high 0x%04X)\n", screen[130][10], border_high);
  printf("  Dwell bar (50, 128)      = 0x%04X (expected high 0x%04X)\n", screen[128][50], border_high);

  /* 2. Pointer moves to Region 1 at (240, 180) */
  printf("Moving pointer to (240, 180)...\n");
  for (int i = 0; i < 30; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 240, 180);
  }
  printf("Final state after moving to Region 1:\n");
  printf("  Region 0 Top-left border (10, 50) = 0x%04X (expected norm 0x%04X)\n", screen[50][10], border_norm);
  printf("  Region 0 Bottom border (10, 130)  = 0x%04X (expected norm 0x%04X)\n", screen[130][10], border_norm);
  printf("  Region 1 Top-left border (10, 149) = 0x%04X (expected high 0x%04X)\n", screen[149][10], border_high);
  assert(screen[50][10] == border_norm);
  assert(screen[130][10] == border_norm);
  assert(screen[149][10] == border_high);
}

static void test_scenario_carousel_switch(void) {
  printf("--- TEST SCENARIO: CAROUSEL DWELL SWITCH ---\n");
  uiMacelleriaInit();

  uint16_t border_norm = HTML2COLOR(0x475569);
  uint16_t border_high = Yellow;

  /* Enter Carousel by dwelling on MACELLERIA (Region 1 at (240, 180)) */
  for (int i = 0; i < 80; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 240, 180);
  }

  /* Now in Carousel! Region 0 (Manzo) is at (12..131, 12..101). Center is ~(70, 56).
   * Region 1 (Pollo) is at (188..307, 12..101). Center is ~(247, 56).
   * 1. Hover on Manzo (Item 0) at (70, 56) */
  for (int i = 0; i < 20; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 70, 56);
  }
  printf("Dwell running on Manzo:\n");
  printf("  Manzo (Item 0) border (12, 12) = 0x%04X (expected high 0x%04X)\n", screen[12][12], border_high);
  printf("  Pollo (Item 1) border (188, 12) = 0x%04X (expected norm 0x%04X)\n", screen[12][188], border_norm);
  assert(screen[12][12] == border_high);
  assert(screen[12][188] == border_norm);

  /* 2. User moves hand fast towards Pollo -> SWIPE_RIGHT arrives! */
  printf("Swipe right arrives while dwell is active on Manzo!\n");
  uiMacelleriaHandleGesture(GESTURE_SWIPE_RIGHT);

  printf("Immediately after SWIPE_RIGHT:\n");
  printf("  Manzo (Item 0) border = 0x%04X (expected norm 0x%04X)\n", screen[12][12], border_norm);
  printf("  Pollo (Item 1) border = 0x%04X (expected high 0x%04X)\n", screen[12][188], border_high);
  assert(screen[12][12] == border_norm);
  assert(screen[12][188] == border_high);

  /* 3. Pointer follows to Pollo */
  for (int i = 0; i < 30; i++) {
    mock_system_time += 20;
    uiMacelleriaTick(true, 247, 56);
  }
  printf("After pointer moves to Pollo:\n");
  printf("  Manzo (Item 0) border = 0x%04X (expected norm 0x%04X)\n", screen[12][12], border_norm);
  printf("  Pollo (Item 1) border = 0x%04X (expected high 0x%04X)\n", screen[12][188], border_high);
  assert(screen[12][12] == border_norm);
  assert(screen[12][188] == border_high);
}

int main(void) {
  test_scenario_pure_hover();
  test_scenario_gesture_nav_conflict();
  test_scenario_carousel_switch();
  return 0;
}
