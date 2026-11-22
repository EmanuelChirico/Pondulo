#include "ui_panels.h"
#include "ui_macelleria.h"
#include "hal.h"
#include "gfx.h"
#include <stdio.h>

#define FOOTER_H          20
#define ANIM_STEPS        6
#define ANIM_FRAME_MS     30
#define PUSH_FLASH_MS     120

typedef enum {
  MODE_NONE = 0,
  MODE_UP,
  MODE_DOWN,
  MODE_LEFT,
  MODE_RIGHT
} display_mode_t;

typedef struct {
  color_t bg;
  const char *label;
} mode_style_t;

/* Indexed [context][mode - 1], mode never MODE_NONE here. */
static const mode_style_t styles[2][4] = {
  {                       /* context 0: CLINICO */
    { Cyan,   "SOPRA" },     /* MODE_UP */
    { Orange, "SOTTO" },     /* MODE_DOWN */
    { Purple, "SINISTRA" },  /* MODE_LEFT */
    { Yellow, "DESTRA" },    /* MODE_RIGHT */
  },
  {                       /* context 1: MACELLERIA */
    { Lime,   "SOPRA" },
    { Maroon, "SOTTO" },
    { Pink,   "SINISTRA" },
    { Teal,   "DESTRA" },
  },
};

static const char *contextNames[2] = { "CLINICO", "MACELLERIA" };

static font_t fontBig;
static font_t fontMed;
static int context = 0;                 /* 0 = CLINICO, 1 = MACELLERIA */
static display_mode_t mode = MODE_NONE; /* last direction selected */

static const char *gestureName(gesture_event_t g) {
  switch (g) {
  case GESTURE_SWIPE_UP:    return "SU";
  case GESTURE_SWIPE_DOWN:  return "GIU";
  case GESTURE_SWIPE_LEFT:  return "SINISTRA";
  case GESTURE_SWIPE_RIGHT: return "DESTRA";
  case GESTURE_PUSH:        return "SELECT";
  case GESTURE_HOLD:        return "HOLD";
  default:                  return "NONE";
  }
}

static void drawFooter(gesture_event_t lastGesture) {
  gCoord w = gdispGetWidth(), h = gdispGetHeight();
  char footer[40];

  snprintf(footer, sizeof(footer), "%s - ultimo gesto: %s", contextNames[context], gestureName(lastGesture));
  gdispFillArea(0, h - FOOTER_H, w, FOOTER_H, Black);
  gdispDrawStringBox(0, h - FOOTER_H, w, FOOTER_H, footer, fontMed, White, justifyCenter);
}

/* Instant (non-animated) redraw of whatever `mode`/`context` currently are -
 * used for the initial screen, after a context change (HOLD) and to restore
 * after the PUSH flash. */
static void drawStaticPanel(void) {
  gCoord w = gdispGetWidth();
  gCoord bodyH = gdispGetHeight() - FOOTER_H;

  if (mode == MODE_NONE) {
    gdispFillArea(0, 0, w, bodyH, Black);
    gdispDrawStringBox(0, bodyH / 2 - 10, w, 24, "In attesa di un gesto...", fontMed, White, justifyCenter);
  } else {
    const mode_style_t *s = &styles[context][mode - 1];
    gdispFillArea(0, 0, w, bodyH, s->bg);
    gdispDrawStringBox(0, bodyH / 2 - 16, w, 32, s->label, fontBig, Black, justifyCenter);
  }
}

/* Wipes the new mode's color panel in from the edge matching the gesture
 * direction, then lands the label. `mode` must already be updated. */
static void animateDirectionalPanel(void) {
  gCoord w = gdispGetWidth();
  gCoord bodyH = gdispGetHeight() - FOOTER_H;
  const mode_style_t *s = &styles[context][mode - 1];
  gCoord prevSize = 0;
  int step;

  for (step = 1; step <= ANIM_STEPS; step++) {
    gCoord size, delta;

    switch (mode) {
    case MODE_UP:       /* wipes down from the top */
      size = (bodyH * step) / ANIM_STEPS;
      delta = size - prevSize;
      gdispFillArea(0, prevSize, w, delta, s->bg);
      break;
    case MODE_DOWN:      /* wipes up from the bottom */
      size = (bodyH * step) / ANIM_STEPS;
      delta = size - prevSize;
      gdispFillArea(0, bodyH - size, w, delta, s->bg);
      break;
    case MODE_LEFT:      /* wipes in from the left edge */
      size = (w * step) / ANIM_STEPS;
      delta = size - prevSize;
      gdispFillArea(prevSize, 0, delta, bodyH, s->bg);
      break;
    case MODE_RIGHT:     /* wipes in from the right edge */
      size = (w * step) / ANIM_STEPS;
      delta = size - prevSize;
      gdispFillArea(w - size, 0, delta, bodyH, s->bg);
      break;
    default:
      return;
    }
    prevSize = size;
    chThdSleepMilliseconds(ANIM_FRAME_MS);
  }

  gdispDrawStringBox(0, bodyH / 2 - 16, w, 32, s->label, fontBig, Black, justifyCenter);
}

static void flashSelect(void) {
  gCoord w = gdispGetWidth();
  gCoord bodyH = gdispGetHeight() - FOOTER_H;

  gdispFillArea(0, 0, w, bodyH, White);
  chThdSleepMilliseconds(PUSH_FLASH_MS);
  drawStaticPanel();
}

void uiPanelsInit(void) {
  fontBig = gdispOpenFont("DejaVuSans24");
  fontMed = gdispOpenFont("DejaVuSans12");

  gdispClear(Black);
  drawStaticPanel();
  drawFooter(GESTURE_NONE);
}

void uiPanelsHandleGesture(gesture_event_t g) {
  if (g == GESTURE_HOLD) {
    context = 1 - context;
    if (context == 1) {
      uiMacelleriaEnter(); /* draws Buongiorno, blocks ~3s, shows the carousel */
    } else {
      drawStaticPanel();
      drawFooter(g);
    }
    return;
  }

  if (context == 1) {
    /* MACELLERIA: item/digit selection goes through the cursor + dwell, not
     * gestures - only SWIPE_LEFT (back) is acted on here. */
    uiMacelleriaHandleGesture(g);
    return;
  }

  switch (g) {
  case GESTURE_SWIPE_UP:    mode = MODE_UP;    animateDirectionalPanel(); break;
  case GESTURE_SWIPE_DOWN:  mode = MODE_DOWN;  animateDirectionalPanel(); break;
  case GESTURE_SWIPE_LEFT:  mode = MODE_LEFT;  animateDirectionalPanel(); break;
  case GESTURE_SWIPE_RIGHT: mode = MODE_RIGHT; animateDirectionalPanel(); break;
  case GESTURE_PUSH:        flashSelect();                                break;
  case GESTURE_NONE:
  default:
    break;
  }

  drawFooter(g);
}

bool uiPanelsIsMacelleria(void) {
  return context == 1;
}
