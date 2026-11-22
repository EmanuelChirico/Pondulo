/*
    ChibiOS - Copyright (C) 2006..2026 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * [NISC2026-GFX01M2-DEMO06] - ChibiOS+uGFX - Gesture-driven mode display.
 *
 * Boots straight into ui_macelleria's Home screen (dwell-select CLINICO or
 * MACELLERIA, driven by the real pointer from cursorSensor over
 * pointer_link/), which then shows a simple confirmation screen - the
 * carousel/keypad/receipt flow is dormant for now (see specs/spec.md).
 * ui/ui_panels.c's old CLINICO directional-panel demo is no longer wired
 * into main() either, for the same reason. GESTURE_HOLD returns to Home
 * from anywhere.
 *
 * GESTURE_LINK_SELFTEST (below) picks the byte source feeding the gesture
 * parser:
 *   1 = software self-test loop, encodes a fixed gesture sequence and feeds
 *       it straight into the same parser used for the real link. Kept on
 *       (today's setting) because the real sensor node (cursorSensor) has
 *       no gesture classification at all - it only streams a continuous
 *       pointer position (see pointer_link/ below), so HOLD/SWIPE for
 *       context-switch and navigation still need a stand-in source.
 *   0 = real link: a thread blocked on sdGet(&SD3). STALE as of the
 *       pointer_link addition below - SD3 now carries the ASCII $XY pointer
 *       stream at 921600 baud, not the 0xAA-framed binary gesture protocol
 *       this path expects, so it would never decode a frame. Left in place
 *       (untouched, still compiles) rather than deleted; see specs/spec.md.
 */
#define GESTURE_LINK_SELFTEST 1

#include "ch.h"
#include "hal.h"
#include "gfx.h"
#include "gesture_link.h"
#include "xy_link.h"
#include "weight_adc.h"
#include "ui_macelleria.h"
#include "mp3_player.h"
#include <stdio.h>

/* Debounce after dispatching a gesture, before the next mailbox message is
 * even looked at. Was 1400ms (fine for the old B1 self-test loop, which
 * only ever advanced one step per physical button press) - way too long
 * once the sensor started classifying real gestures itself (2026-09-05):
 * a second genuine swipe attempted inside that window just sat queued,
 * felt "not picked up", and with several queued up the gesture mailbox
 * (see GESTURE_MAILBOX_SIZE below) would fill, see the non-blocking-post
 * comment in pointer_link/src/xy_link.c for what that does to the pointer
 * stream. 200ms is enough to not double-fire on the same physical
 * gesture without meaningfully lagging deliberate repeated ones. */
#define GESTURE_DELAY_MS    200
#define GESTURE_MAILBOX_SIZE   8
#define UI_TICK_MS             20   /* ~50Hz: dwell-progress animation + cursor overlay - was 33ms/30Hz, tightened to cut perceived pointer lag */

/* SD3 now carries the pointer_link ($XY) stream from cursorSensor, matching
 * its keypad_link.c config exactly (921600 8N1). No longer the gesture_link
 * baud - see the GESTURE_LINK_SELFTEST comment above. */
static const SerialConfig pointer_serial_cfg = {
  921600,
  0,
  USART_CR2_STOP1_BITS,
  0
};

static msg_t mbBuf[GESTURE_MAILBOX_SIZE];
static mailbox_t gestureMailbox;

#if GESTURE_LINK_SELFTEST

/* Fake incoming stream: two passes (one per context) of UP/RIGHT/PUSH/DOWN/
 * LEFT/PUSH, each pass closed by a HOLD that flips context. Encoded through
 * gestureLinkEncode() and fed byte-by-byte through the real parser, so this
 * exercises the exact same decode path the UART RX thread will use. */
static const gesture_event_t demoGestures[] = {
  GESTURE_SWIPE_UP, GESTURE_SWIPE_RIGHT, GESTURE_PUSH,
  GESTURE_SWIPE_DOWN, GESTURE_SWIPE_LEFT, GESTURE_PUSH,
  GESTURE_HOLD,
  GESTURE_SWIPE_RIGHT, GESTURE_SWIPE_UP, GESTURE_PUSH,
  GESTURE_SWIPE_LEFT, GESTURE_SWIPE_DOWN, GESTURE_PUSH,
  GESTURE_HOLD,
};
#define DEMO_GESTURE_COUNT (int)(sizeof(demoGestures) / sizeof(demoGestures[0]))

/* Advances one step through demoGestures[] per press of B1 (PC13, active
 * low, matches cursorSensor's own button convention) instead of firing the
 * whole sequence on a fixed automatic cadence. That way HOLD (context
 * switch into MACELLERIA) happens on demand, and nothing else auto-fires
 * once you stop pressing - needed to hold still in MACELLERIA for as long
 * as it takes to exercise the real pointer_link/dwell-selection UI, which
 * a fixed ~1.4s*N auto-loop would otherwise yank back to CLINICO every
 * ~10s regardless of what you were doing. */
/* NOTE: on this board, PC13 has been confirmed (via a since-removed on-
 * screen poll-count/raw-state diagnostic) to never actually read LOW when
 * B1 is physically pressed - a hardware-level issue, not a firmware bug.
 * DebugGestureThread below (SD2 keyboard input) is the reliable gesture
 * source in the meantime; this loop is left in place in case the button
 * issue is ever fixed at the hardware level. */
static THD_WORKING_AREA(waGestureSourceThread, 1024);
static THD_FUNCTION(GestureSourceThread, arg) {
  gesture_link_parser_t parser;
  int i = 0;

  (void)arg;
  chRegSetThreadName("gesture_selftest");
  gestureLinkParserInit(&parser);

  while (true) {
    int j;
    uint8_t frame[5];
    uint8_t n;

    while (palReadPad(GPIOC, 13U) == PAL_HIGH) {
      chThdSleepMilliseconds(20);
    }
    chThdSleepMilliseconds(60); /* debounce */
    if (palReadPad(GPIOC, 13U) != PAL_LOW) {
      continue; /* bounced back high before settling - not a real press */
    }
    while (palReadPad(GPIOC, 13U) == PAL_LOW) {
      chThdSleepMilliseconds(20); /* wait for release */
    }

    n = gestureLinkEncode(demoGestures[i], frame);
    for (j = 0; j < n; j++) {
      gesture_event_t g;
      if (gestureLinkParserFeed(&parser, frame[j], &g))
        chMBPostTimeout(&gestureMailbox, (msg_t)g, TIME_MS2I(100));
    }

    i = (i + 1) % DEMO_GESTURE_COUNT;
  }
}

#else /* !GESTURE_LINK_SELFTEST */

static THD_WORKING_AREA(waGestureSourceThread, 1024);
static THD_FUNCTION(GestureSourceThread, arg) {
  gesture_link_parser_t parser;

  (void)arg;
  chRegSetThreadName("gesture_uart_rx");
  gestureLinkParserInit(&parser);

  while (true) {
    msg_t b = sdGet(&SD3);

    if (b >= MSG_OK) {
      gesture_event_t g;
      if (gestureLinkParserFeed(&parser, (uint8_t)b, &g))
        chMBPostTimeout(&gestureMailbox, (msg_t)g, TIME_MS2I(100));
    }
  }
}

#endif /* GESTURE_LINK_SELFTEST */

/* Reliable alternative to B1 (PC13 never reads a press on this board -
 * confirmed via the button_poll_count/button_last_raw_high diagnostic
 * above, which shows GestureSourceThread alive and looping but the pin
 * stuck HIGH regardless of physical presses - a hardware-level issue, not
 * fixable from here). Reads single keystrokes from SD2 (the ST-LINK VCP -
 * open it in a serial terminal, e.g. PuTTY, at 115200 8N1) and posts the
 * matching gesture to the same mailbox as the button source, so both can
 * coexist: u=UP d=DOWN l=LEFT r=RIGHT p=PUSH h=HOLD.
 * SD2 is started with sdStart(&SD2, NULL) below, i.e. SERIAL_DEFAULT_BITRATE
 * from cfg/halconf.h - 38400 8N1, not the more common 115200. */
static THD_WORKING_AREA(waDebugGestureThread, 1024);
static THD_FUNCTION(DebugGestureThread, arg) {
  (void)arg;
  chRegSetThreadName("gesture_debug_key");

  while (true) {
    msg_t ch = sdGetTimeout(&SD2, TIME_MS2I(50));
    gesture_event_t g;

    if (ch < MSG_OK) {
      continue;
    }

    switch ((char)ch) {
    case 'u': g = GESTURE_SWIPE_UP;    break;
    case 'd': g = GESTURE_SWIPE_DOWN;  break;
    case 'l': g = GESTURE_SWIPE_LEFT;  break;
    case 'r': g = GESTURE_SWIPE_RIGHT; break;
    case 'p': g = GESTURE_PUSH;        break;
    case 'h': g = GESTURE_HOLD;        break;
    default:  continue; /* ignore anything else (newlines, etc.) */
    }

    chMBPostTimeout(&gestureMailbox, (msg_t)g, TIME_MS2I(100));
  }
}

/* Continuously drains SD3 into pointer_link's internal "latest message" -
 * a latest-value stream (like cursorSensor's own dirty+heartbeat sender),
 * not discrete events, so no mailbox is needed here. */
static THD_WORKING_AREA(waPointerLinkThread, 1024);
static THD_FUNCTION(PointerLinkThread, arg) {
  (void)arg;
  chRegSetThreadName("pointer_link_rx");

  while (true) {
    xy_link_poll(TIME_MS2I(20));
  }
}

/* Periodic (~30Hz) refresh for dwell-progress animation and the cursor
 * overlay - the design explicitly calls for this not to be purely
 * event-driven. ui_macelleria now owns the entire top-level UI (Home, the
 * simple screen, and the dormant carousel/keypad/receipt flow), so this
 * always ticks - no context gate needed anymore. */
/* 256 (the original template default for a simple non-rendering thread) is
 * not enough here: this thread calls into uGFX text/box drawing plus
 * snprintf with several values, both of which use nontrivial stack. On a
 * Cortex-M without an MPU stack guard, overflowing this quietly corrupts
 * adjacent RAM instead of faulting cleanly - a very plausible explanation
 * for the erratic pointer_link symptoms seen while this was still 256. */
static THD_WORKING_AREA(waUiTickThread, 4096);
static THD_FUNCTION(UiTickThread, arg) {
  (void)arg;
  chRegSetThreadName("ui_tick");

  while (true) {
    xy_msg_t m;
    xy_link_get_last_msg(&m);
    uiMacelleriaTick(m.valid != 0, m.x, m.y);
    /* Live scale reading: a single blocking ADC conversion (a few
     * microseconds, see weight_adc.c) of the voltage bilanciaFinale's DAC
     * is driving. uiMacelleriaSetLiveWeight() only actually writes into
     * item_weight_grams[] while the Bilancia screen is up, so reading it
     * unconditionally every tick is harmless the rest of the time. */
    uiMacelleriaSetLiveWeight(weight_adc_get_grams());
    chThdSleepMilliseconds(UI_TICK_MS);
  }
}

/* TEMPORARY diagnostic (2026-09-04): prints SD3/pointer_link counters once a
 * second on SD2 (ST-LINK VCP, 38400 8N1 - open a terminal on the board's
 * COM port) to localize a "cursor not moving on screen" report:
 *   raw stuck at 0            -> no bytes ever reach SD3 (wiring/power/
 *                                 cursorSensor board, not this firmware)
 *   raw growing, frames stuck -> bytes arrive but never parse as a valid
 *                                 $XY line (baud/pin mismatch or protocol
 *                                 drift - check err flags too)
 *   frames growing steadily   -> pointer_link is fine, the bug (if any) is
 *                                 in ui_macelleria's rendering, not the link
 * Safe to remove once the report above is resolved. */
static THD_WORKING_AREA(waLinkDiagThread, 1024);
static THD_FUNCTION(LinkDiagThread, arg) {
  (void)arg;
  chRegSetThreadName("link_diag");

  while (true) {
    chThdSleepMilliseconds(1000);
    printf("[xy_link] raw_bytes=%lu frames=%lu err_flags=0x%lx\r\n",
           (unsigned long)xy_link_get_raw_byte_count(),
           (unsigned long)xy_link_get_frame_count(),
           (unsigned long)xy_link_get_sticky_errors());
    uint16_t diag_grams = weight_adc_get_grams(); /* triggers the conversion */
    printf("[weight_adc] raw=%u grams=%u\r\n",
           (unsigned)weight_adc_get_raw_sample(), (unsigned)diag_grams);
  }
}

int main(void) {

  halInit();
  chSysInit();

  sdStart(&SD2, NULL);                  /* USART2 -> ST-LINK VCP, backs printf/chprintf */
  sdStart(&SD3, &pointer_serial_cfg);   /* USART3 -> pointer link from cursorSensor */
  xy_link_init();

  /* USART2 GPIOs (ST-LINK VCP on PA2/PA3). REQUIRED despite sdStart() above:
   * this board's ST_NUCLEO64_G474RE/board.h defaults PA2/PA3 to AF12
   * (LPUART1), not AF7 (USART2) - without this override SD2 runs
   * internally but its signals never reach the physical pins, so nothing
   * sent/received over the debug VCP works at all (confirmed: this was
   * silently broken the whole time, including the DebugGestureThread
   * keyboard-gesture path below, until this was added). */
  palSetPadMode(GPIOA, 2U, PAL_MODE_ALTERNATE(7));
  palSetPadMode(GPIOA, 3U, PAL_MODE_ALTERNATE(7));

  /* USART3 GPIOs: PC10 = TX, PC11 = RX (AF7). Free of conflicts with the
   * ILI9341 shield's SPI1/GPIO pins (see board_ILI9341.h). OSPEED_HIGHEST
   * matches cursorDisplay's xy_link_init() exactly - at 921600 baud a slow
   * pin slew rate between two separate boards (wires, not a PCB trace) can
   * corrupt bits without the receiving parser ever seeing a valid frame. */
  palSetPadMode(GPIOC, 10U, PAL_MODE_ALTERNATE(7) | PAL_STM32_OSPEED_HIGHEST);
  palSetPadMode(GPIOC, 11U, PAL_MODE_ALTERNATE(7) | PAL_STM32_OSPEED_HIGHEST);

  /* B1 user button (PC13), for the button-stepped self-test gesture source
   * above - matches cursorSensor's own INPUT_PULLUP/active-low convention. */
  palSetPadMode(GPIOC, 13U, PAL_MODE_INPUT_PULLUP);

  /* Analog voltage RX (PA4/"A2", ADC2_IN17) from the separate load-cell/
   * display board - see weight_adc.h for why this replaced a PWM link
   * (which itself had replaced a serial link - see REPORT.md for the full
   * history). weight_adc_init() configures the pad as analog input and
   * starts ADCD2, so nothing else is needed here. */
  weight_adc_init();

  gfxInit();
  gdispSetOrientation(gOrientation270);

  uiMacelleriaInit(); /* draws the Home screen - this is the boot entry point */

  /* Inizializzazione sincrona del Serial MP3 Player YX5300 (UART5, ~1.1s boot) */
  mp3_init();

  chMBObjectInit(&gestureMailbox, mbBuf, GESTURE_MAILBOX_SIZE);
  xy_link_set_gesture_mailbox(&gestureMailbox);
  chThdCreateStatic(waGestureSourceThread, sizeof(waGestureSourceThread), NORMALPRIO, GestureSourceThread, NULL);
  chThdCreateStatic(waDebugGestureThread, sizeof(waDebugGestureThread), NORMALPRIO, DebugGestureThread, NULL);
  chThdCreateStatic(waPointerLinkThread, sizeof(waPointerLinkThread), NORMALPRIO, PointerLinkThread, NULL);
  chThdCreateStatic(waUiTickThread, sizeof(waUiTickThread), NORMALPRIO, UiTickThread, NULL);
  chThdCreateStatic(waLinkDiagThread, sizeof(waLinkDiagThread), NORMALPRIO, LinkDiagThread, NULL);

  while (true) {
    msg_t msg;

    if (chMBFetchTimeout(&gestureMailbox, &msg, TIME_INFINITE) == MSG_OK) {
      uiMacelleriaHandleGesture((gesture_event_t)msg);
      chThdSleepMilliseconds(GESTURE_DELAY_MS);
    }
  }
}
