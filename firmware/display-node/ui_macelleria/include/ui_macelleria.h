/*
 * PONDULO retail and clinical demonstration interface.
 * Home -> product carousel -> live weight -> quantity -> cart / receipt.
 * Driven by pointer position with dwell-selection and discrete gestures.
 * See docs/ARCHITECTURE.md at the repository root.
 */

#ifndef UI_MACELLERIA_H
#define UI_MACELLERIA_H

#include <stdint.h>
#include <stdbool.h>
#include "gesture_link.h"

#define CAROUSEL_ITEM_NAME_LEN 24U
#define CAROUSEL_MAX_ITEMS     8U

typedef struct {
  char nome[CAROUSEL_ITEM_NAME_LEN];
  uint16_t prezzo_centesimi_al_kg;
} carousel_item_t;

/* Opens fonts, resets internal state and draws the initial Home screen
 * (two dwell-selectable zones: CLINICO / MACELLERIA). Call once after
 * gfxInit() - this module now owns the entire top-level UI, from boot. */
void uiMacelleriaInit(void);

/* Called at ~50 Hz (20 ms in main.c). Updates hover, dwell and the cursor
 * incrementally. valid=false resets an in-progress dwell selection. */
void uiMacelleriaTick(bool valid, uint16_t x, uint16_t y);

/* Routes swipes, PUSH, HOLD and PRESENCE for the current screen.
 * HOLD returns Home; screen-specific gestures complement pointer dwell. */
void uiMacelleriaHandleGesture(gesture_event_t g);

/* Stub for a future WiFi/ESP8266 price-list client: replaces the current
 * item array (up to CAROUSEL_MAX_ITEMS, extra entries ignored) and, if the
 * carousel screen is currently visible, redraws it immediately. Not called
 * from anywhere yet. */
void carousel_update_items(const carousel_item_t *nuovi_item, uint8_t count);

/* Feeds a live weight reading (grams) for the currently-selected item into
 * the Bilancia screen - the actual transport (weight_adc's ADC reading of
 * the separate load-cell board's DAC output) lives in main.c, kept out of
 * this file on purpose, the same way pointer x/y arrives as plain
 * parameters to uiMacelleriaTick() rather than this module depending on
 * xy_link.h. Only has any visible effect while MAC_STATE_BILANCIA is
 * showing that item; harmless (and cheap) to call from anywhere else. */
void uiMacelleriaSetLiveWeight(uint16_t grams);

#endif /* UI_MACELLERIA_H */
