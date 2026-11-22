/*
 * ui_panels - screen/panel presentation for the gesture-driven display.
 *
 * Each directional gesture maps to its own full-panel screen (window-switch
 * style, not a scrolling carousel): SWIPE_UP/DOWN/LEFT/RIGHT jump straight
 * to that panel. PUSH flashes (select feedback), HOLD toggles between two
 * operating contexts. Pulled out of main.c so main.c is just init + thread
 * wiring; this module owns all gdisp* calls and all UI state.
 */

#ifndef UI_PANELS_H
#define UI_PANELS_H

#include "gesture_link.h"

/* Opens fonts and draws the initial (waiting) screen. Call after gfxInit(). */
void uiPanelsInit(void);

/* Applies one decoded gesture to the screen (panel switch / flash / context
 * toggle / footer update), per the mapping above. In the MACELLERIA context
 * this delegates to ui_macelleria instead of the directional-panel logic. */
void uiPanelsHandleGesture(gesture_event_t g);

/* True when the current context is MACELLERIA (vs CLINICO). Polled by
 * UiTickThread in main.c to decide whether to feed the latest pointer
 * sample into ui_macelleria's dwell-selection tick. */
bool uiPanelsIsMacelleria(void);

#endif /* UI_PANELS_H */
