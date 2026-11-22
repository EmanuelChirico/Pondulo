#include "ch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "gfx.h"
#include "ui_macelleria.h"
#include "mp3_player.h"

systime_t mock_system_time = 1000;

#define SCREEN_W 320
#define SCREEN_H 240

gCoord gdispGetWidth(void) { return SCREEN_W; }
gCoord gdispGetHeight(void) { return SCREEN_H; }
font_t gdispOpenFont(const char *name) { (void)name; return (font_t)1; }
void gdispSetClip(gCoord x, gCoord y, gCoord cx, gCoord cy) { (void)x; (void)y; (void)cx; (void)cy; }
void gdispUnsetClip(void) {}

void gdispFillArea(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color) {
  (void)x; (void)y; (void)cx; (void)cy; (void)color;
}
void gdispDrawBox(gCoord x, gCoord y, gCoord cx, gCoord cy, gColor color) {
  (void)x; (void)y; (void)cx; (void)cy; (void)color;
}
void gdispDrawStringBox(gCoord x, gCoord y, gCoord cx, gCoord cy, const char *str, font_t f, gColor color, gJustify j) {
  (void)x; (void)y; (void)cx; (void)cy; (void)str; (void)f; (void)color; (void)j;
}
void gdispDrawCircle(gCoord x, gCoord y, gCoord radius, gColor color) {
  (void)x; (void)y; (void)radius; (void)color;
}
void gdispFillCircle(gCoord x, gCoord y, gCoord radius, gColor color) {
  (void)x; (void)y; (void)radius; (void)color;
}
void gdispDrawLine(gCoord x0, gCoord y0, gCoord x1, gCoord y1, gColor color) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}
void gdispDrawPixel(gCoord x, gCoord y, gColor color) {
  (void)x; (void)y; (void)color;
}
void gdispBlitArea(gCoord x, gCoord y, gCoord cx, gCoord cy, const gColor *buffer) {
  (void)x; (void)y; (void)cx; (void)cy; (void)buffer;
}

/* Audio log harness */
static uint16_t audio_log[128];
static int audio_log_count = 0;

void mp3_play_track(uint16_t track) {
  if (audio_log_count < (int)(sizeof(audio_log) / sizeof(audio_log[0]))) {
    audio_log[audio_log_count++] = track;
  }
}

static void audio_log_clear(void) {
  audio_log_count = 0;
  memset(audio_log, 0, sizeof(audio_log));
}

/* Current UI: vertical Home, live scale, quantity keypad (1..99). */
static void dwell(uint16_t x, uint16_t y) {
  for (int i = 0; i < 80; ++i) {
    mock_system_time += 20;
    uiMacelleriaTick(true, x, y);
  }
}

static void expect_track(uint16_t track) {
  assert(audio_log_count == 1);
  assert(audio_log[0] == track);
  audio_log_clear();
}

int main(void) {
  audio_log_clear();
  uiMacelleriaInit();
  assert(audio_log_count == 0);
  uiMacelleriaHandleGesture(GESTURE_PRESENCE);
  expect_track(SOUND_BENVENUTO_MENU);

  dwell(240, 180); /* MACELLERIA on the vertical Home */
  assert(audio_log_count == 0);
  uiMacelleriaHandleGesture(GESTURE_PRESENCE); /* no greeting outside Home */
  assert(audio_log_count == 0);
  dwell(70, 56); /* Manzo */
  expect_track(SOUND_DING_DONG);
  uiMacelleriaSetLiveWeight(250);
  assert(audio_log_count == 0);

  /* The quantity block is PUSH-only: lingering there must not open it. */
  dwell(160, 100);
  assert(audio_log_count == 0);
  uiMacelleriaHandleGesture(GESTURE_PUSH);
  assert(audio_log_count == 0);
  dwell(160, 118); /* digit 5 */
  expect_track(SOUND_TASTO_BIP);
  dwell(260, 215); /* confirm quantity, returns to scale */
  expect_track(SOUND_TASTO_BIP);
  dwell(240, 204); /* on-screen receipt */
  expect_track(SOUND_CASSA);

  uiMacelleriaHandleGesture(GESTURE_HOLD);
  uiMacelleriaHandleGesture(GESTURE_PRESENCE);
  expect_track(SOUND_BENVENUTO_MENU);
  puts("PASS: presence, scale entry, live weight, quantity keypad and receipt audio");
  return 0;
}
