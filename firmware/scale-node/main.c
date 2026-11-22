/*
    NeaPolis Innovation Summer Campus Play Embedded Porting
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
 * [NISC2026-CL3641BH-2x74HC595 + HX711] - Scale firmware.
 *
 * Shows the weight in grams as "XXXXXX.YY" (dpPos=5, e.g. half a gram
 * = "0.50", negative values shown with a leading minus). Live-updates
 * for 4.5s, then freezes on the average of 15 samples taken over ~2s,
 * until the weight changes by >2g (item added/removed), which
 * restarts the cycle. If that settled average comes back within ~1g of
 * zero, the scale re-tares on it and shows an exact 0.00 instead of
 * leftover sensor noise/creep (auto-zero tracking, like a real scale).
 *
 * The very first measurement *is* the tare (assumes the scale is
 * empty at boot, like a real kitchen scale): its raw reading becomes
 * the zero point, so that first result is always exactly 0.00 --
 * instead of a fixed constant, so overnight temperature/creep drift
 * doesn't throw off the reading. The Knight Rider scanner runs on the
 * display for the whole boot-time tare (filter warmup + settle +
 * averaging), as a "still working" indicator -- and only for exactly
 * that long, see thdTareIndicator below.
 *
 * CALIBRATION (empirical, this physical unit -- redo if the load
 * cell, HX711 board or wiring changes):
 *   delta = 59240 counts per 68.5g
 *   centigrams = (raw - tareRaw) * 6850 / 59240
 *
 * WIRING:
 *   Display: 5V->5V GND->GND DIO->PB5(D4) SCK->PB3(D3) RCK->PC0(A5)
 *   HX711:   VCC->5V GND->GND DT->PB4(D5) SCK->PB10(D6)
 *   Link to DEMO6 display board: PA4 (DAC1_OUT1) -> DEMO6 PA4
 *   (ADC2_IN17, "A2"), GND->GND. A real analog voltage now (2026-09-05,
 *   see weight_dac.h/.c) - 0V = 0g, ~VDDA = 1000g, linear in between.
 *   Two earlier approaches were tried and abandoned here, see REPORT.md
 *   for the full history: a USART3 serial line (failed once the real ToF
 *   sensor board shared DEMO6's own hardware USART3 - even USART3's
 *   receiver started showing framing errors, ruling out any
 *   interrupt-timing explanation), then a PWM duty-cycle link on this
 *   same PA4/PA4 pin pair (worked, but its decoded value jittered a few
 *   grams even at a stable weight, driving distracting constant screen
 *   redraws on DEMO6). A DAC->ADC pair removes every timing concern at
 *   once - it is just a DC voltage, nothing to sample or decode a ratio
 *   from. One-way only.
 */
#include "ch.h"
#include "hal.h"

#include "cl3641bh8.h"
#include "hx711.h"
#include "weight_dac.h"

#define LINE_ARD_D4  PAL_LINE(GPIOB, 5U)   // display DIO/MOSI
#define LINE_ARD_D3  PAL_LINE(GPIOB, 3U)   // display SCK
#define LINE_ARD_A5  PAL_LINE(GPIOC, 0U)   // display RCK
#define LINE_ARD_D5  PAL_LINE(GPIOB, 4U)   // HX711 DT
#define LINE_ARD_D6  PAL_LINE(GPIOB, 10U)  // HX711 SCK

/* Sends the current weight (centigrams -> whole grams, clamped
 * non-negative) to DEMO6 by setting the DAC output voltage - see
 * weight_dac.c. A single register write, immediate, never blocks - no
 * thread involved at all (unlike the PWM version this replaced). */
static void sendWeightToDisplay(int64_t centigrams) {

  int32_t grams = (int32_t)(centigrams / 100);
  if (grams < 0) {
    grams = 0; /* the scale never reports a negative weight to the display */
  }
  else if (grams > (int32_t)WEIGHT_DAC_MAX_GRAMS) {
    grams = (int32_t)WEIGHT_DAC_MAX_GRAMS;
  }

  weightDacSetGrams((uint16_t)grams);
}

#define DP_POS 5  /* "XXXXXX.YY" grams */

/* Calibration -- see CALIBRATION above. tareRaw is measured at boot. */
#define CAL_KNOWN_CENTIGRAMS  6850L
#define CAL_DELTA_COUNTS      59240L

static int32_t tareRaw;

#define FILTER_SAMPLES 16  /* smooths each raw read */

static int32_t filterBuf[FILTER_SAMPLES];
static unsigned filterCount = 0;
static unsigned filterIdx = 0;

/* hx711Read() can now return HX711_TIMEOUT instead of blocking forever if
 * the chip doesn't respond in time (2026-09-05, see hx711.c) - on a timeout
 * this skips the sample entirely (does not touch filterBuf/filterIdx) and
 * just returns the existing average unchanged, instead of either freezing
 * this thread or feeding INT32_MIN into the running average and wrecking
 * every reading derived from it for the next FILTER_SAMPLES calls. */
static int32_t hx711ReadFiltered(void) {

  int32_t sample = hx711Read();
  if (sample != HX711_TIMEOUT) {
    filterBuf[filterIdx] = sample;
    filterIdx = (filterIdx + 1U) % FILTER_SAMPLES;
    if (filterCount < FILTER_SAMPLES) {
      filterCount++;
    }
  }

  if (filterCount == 0U) {
    return 0; /* no good reading yet at all (e.g. HX711 not up right at boot) */
  }

  int64_t sum = 0;
  for (unsigned ii = 0; ii < filterCount; ii++) {
    sum += filterBuf[ii];
  }
  return (int32_t)(sum / (int64_t)filterCount);
}

static int64_t weightCg(void) {

  return ((int64_t)(hx711ReadFiltered() - tareRaw) * CAL_KNOWN_CENTIGRAMS) /
         CAL_DELTA_COUNTS;
}

#define I64_ABS(v)             (((v) < 0) ? -(v) : (v))
#define CHANGE_TOLERANCE_CG    100  /* ~1g: bigger jump restarts the cycle */
#define AUTO_ZERO_THRESHOLD_CG 100  /* ~1g: settle this close to 0 -> snap to exact 0.00 */

/* "Knight Rider" easter egg (cl3641bh8Knightrider(), see userlib) --
 * hold B1 for KNIGHTRIDER_LONGPRESS_MS to fire it. Only polled in the
 * idle wait-for-change loop below, on purpose: the effect blocks the
 * calling thread for its whole duration, and that loop is the one
 * place in main() where nothing time-critical (taring, live/hold
 * averaging) is in progress, so blocking there for a couple of
 * seconds doesn't disturb the scale's own behavior. It does mean a
 * weight placed *during* the effect isn't noticed until it ends. */
#define KNIGHTRIDER_LONGPRESS_MS  1000U  /* how long B1 must stay held */
#define KNIGHTRIDER_SWEEPS        3U     /* full left-right-left passes */
#define KNIGHTRIDER_STEP_MS       300U   /* per-digit delay -- speed knob */

/* Renders a (possibly negative) weight. In live mode (blankLeading =
 * false) it behaves like cl3641bh8SetNumberDp(): leading zeros stay
 * visible. In hold mode (blankLeading = true) unused leading digits
 * are turned off instead (e.g. "68.50" instead of "000068.50"). A
 * negative value always shows a minus sign right before the number,
 * sacrificing one leading digit's magnitude for it -- never an issue
 * here since a small kitchen-scale reading never gets close to using
 * all the digits. */
static void renderWeight(int64_t value, unsigned dpPos, bool blankLeading) {

  bool negative = value < 0;
  uint32_t mag = (uint32_t)(negative ? -value : value) % 100000000UL;

  uint8_t digits[CL3641BH8_NUM_DIGITS];
  uint32_t v = mag;
  for (int ii = CL3641BH8_NUM_DIGITS - 1; ii >= 0; ii--) {
    digits[ii] = (uint8_t)(v % 10U);
    v /= 10U;
  }

  unsigned firstShown = 0;
  if (blankLeading) {
    firstShown = dpPos;
    for (unsigned ii = 0; ii <= dpPos; ii++) {
      if (digits[ii] != 0U) {
        firstShown = ii;
        break;
      }
    }
  }
  if (negative && firstShown == 0U) {
    firstShown = 1U;  /* make room for the sign */
  }

  for (unsigned ii = 0; ii < CL3641BH8_NUM_DIGITS; ii++) {
    if (negative && ii == firstShown - 1U) {
      cl3641bh8SetDigit(ii, CL3641BH8_MINUS, false);
    }
    else if (ii < firstShown) {
      cl3641bh8SetDigit(ii, CL3641BH8_BLANK, false);
    }
    else {
      cl3641bh8SetDigit(ii, digits[ii], ii == dpPos);
    }
  }
}

/* Knight Rider as a "taring in progress" indicator for the boot-time
 * tare sequence: runs on its own thread (main() itself is busy blocking
 * on HX711 reads throughout that sequence, and nothing else touches
 * the display during it, so there's no race) and is told to stop -- via
 * tareInProgress, checked at every single step, not just between
 * sweeps -- the instant tareRaw is computed, so it never overruns the
 * real tare duration by more than one step. */
static THD_WORKING_AREA(waTareIndicator, 512);
static volatile bool tareInProgress = false;

static THD_FUNCTION(thdTareIndicator, arg) {

  (void)arg;
  chRegSetThreadName("tare-indicator");
  cl3641bh8KnightriderUntil(&tareInProgress, KNIGHTRIDER_STEP_MS);
}

int main(void) {

  halInit();
  chSysInit();

  cl3641bh8Start(&SPID1, LINE_ARD_D3, LINE_ARD_D4, LINE_ARD_A5);
  hx711Start(LINE_ARD_D5, LINE_ARD_D6);

  weightDacStart(); /* PA4 -> DEMO6 PA4, see weight_dac.h */

  tareInProgress = true;
  thread_t *tareIndicatorThd = chThdCreateStatic(waTareIndicator,
                                                  sizeof(waTareIndicator),
                                                  NORMALPRIO, thdTareIndicator,
                                                  NULL);

  /* Warm up the filter, then let the cell settle for a moment before
   * anything is measured for real. */
  for (unsigned ii = 0; ii < FILTER_SAMPLES; ii++) {
    hx711ReadFiltered();
  }
  chThdSleepMilliseconds(1000);

  /* First measurement = the tare: whatever the (assumed empty) scale
   * reads right now becomes the zero point, so this first result is
   * 0.00 by construction. */
  int64_t rawSum = 0;
  for (unsigned ii = 0; ii < 15U; ii++) {
    rawSum += hx711ReadFiltered();
    chThdSleepMilliseconds(133);
  }
  tareRaw = (int32_t)(rawSum / 15);

  /* Stop the indicator and wait for it to actually restore the display
   * and exit before rendering the real first value below -- otherwise
   * the two could race on the same digit arrays. */
  tareInProgress = false;
  chThdWait(tareIndicatorThd);

  int64_t holdValue = 0;
  renderWeight(holdValue, DP_POS, true);
  sendWeightToDisplay(holdValue);

  bool buttonHeld = false;
  bool knightriderFired = false;
  systime_t buttonPressStart = 0;

  while (true) {
    /* Stay frozen until the weight actually changes. */
    while (I64_ABS(weightCg() - holdValue) <= CHANGE_TOLERANCE_CG) {

      /* B1 is active-LOW on the Nucleo64 (idle HIGH via pull-up on the
       * board, pressed = GND). Fires cl3641bh8Knightrider() once per
       * press-and-hold; the button must be released before it can
       * fire again (no repeat-fire while held down). */
      bool buttonPressed = palReadLine(LINE_BUTTON) == PAL_LOW;
      if (buttonPressed && !buttonHeld) {
        buttonHeld = true;
        knightriderFired = false;
        buttonPressStart = chVTGetSystemTimeX();
      }
      else if (buttonPressed && buttonHeld && !knightriderFired &&
               chVTTimeElapsedSinceX(buttonPressStart) >=
                   TIME_MS2I(KNIGHTRIDER_LONGPRESS_MS)) {
        knightriderFired = true;
        cl3641bh8Knightrider(KNIGHTRIDER_SWEEPS, KNIGHTRIDER_STEP_MS);
      }
      else if (!buttonPressed) {
        buttonHeld = false;
        knightriderFired = false;
      }

      /* Keep DEMO6 in sync with the frozen value even while nothing is
       * changing here - otherwise it would only ever hear about a weight
       * during the 4.5s live bursts below. */
      sendWeightToDisplay(holdValue);
      chThdSleepMilliseconds(200);
    }

    /* Live phase: update continuously for 4.5s. Deliberately NOT sent to
     * DEMO6 (sendWeightToDisplay()) here: the reading is still jumping around
     * while the item settles on the load cell, and DEMO6 should only ever
     * show the processed/averaged result below - not raw live jitter. */
    systime_t start = chVTGetSystemTimeX();
    while (chVTTimeElapsedSinceX(start) < TIME_MS2I(4500)) {
      renderWeight(weightCg(), DP_POS, false);
    }

    /* Average 15 raw samples over ~2s. */
    int64_t holdRawSum = 0;
    for (unsigned ii = 0; ii < 15U; ii++) {
      holdRawSum += hx711ReadFiltered();
      chThdSleepMilliseconds(133);
    }
    int32_t holdRawAvg = (int32_t)(holdRawSum / 15);
    holdValue = ((int64_t)(holdRawAvg - tareRaw) * CAL_KNOWN_CENTIGRAMS) /
                CAL_DELTA_COUNTS;

    /* Auto-zero tracking: if we settled back this close to zero, the
     * scale is almost certainly empty again -- re-tare on this reading
     * so it shows an exact 0.00 (and any slow drift since boot is
     * cancelled too), instead of displaying leftover sensor noise. */
    if (I64_ABS(holdValue) < AUTO_ZERO_THRESHOLD_CG) {
      tareRaw = holdRawAvg;
      holdValue = 0;
    }

    renderWeight(holdValue, DP_POS, true);
    sendWeightToDisplay(holdValue);
  }
}
