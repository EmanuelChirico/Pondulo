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

/**
 * @file    cl3641bh8.c
 * @brief   High-level driver for two CL3641BH 4-digit 7-segment displays
 *          multiplexed through the RobotDyn "8-Digit LED Display 74HC595"
 *          module. See cl3641bh8.h and README.md for details.
 *
 * @addtogroup cl3641bh8
 * @{
 */

#include "ch.h"
#include "hal.h"

#include "hc595.h"
#include "cl3641bh8.h"

/*===========================================================================*/
/* Protocol constants determined empirically -- see README.md.              */
/*===========================================================================*/

#define SEG_ACTIVE_LOW      TRUE   /* segment byte: bit=0 lights the segment */
#define DIGIT_ACTIVE_LOW    FALSE  /* digit-select byte: bit=1 enables it   */

/* Segment font, active-HIGH convention (bit=1 lights the segment),
 * standard bit order confirmed on this unit: bit0=A bit1=B bit2=C
 * bit3=D bit4=E bit5=F bit6=G bit7=DP. */
static const uint8_t font[10] = {
  0x3F, /**< 0 */
  0x06, /**< 1 */
  0x5B, /**< 2 */
  0x4F, /**< 3 */
  0x66, /**< 4 */
  0x6D, /**< 5 */
  0x7D, /**< 6 */
  0x07, /**< 7 */
  0x7F, /**< 8 */
  0x6F  /**< 9 */
};

#define MINUS_SEGBYTE  0x40U  /**< segment G only -- looks like a minus sign */

/* Private glyphs for cl3641bh8Knightrider() only -- never reachable
 * through the public API (cl3641bh8SetDigit() reduces any value other
 * than BLANK/MINUS mod 10 before storing it), so 0xFD can't collide
 * with a real digit. */
#define KNIGHTRIDER_EYE_VALUE    0xFDU  /**< sentinel: the bright "eye" glyph */
#define KNIGHTRIDER_EYE_SEGBYTE  0x36U  /**< segments B+C+E+F: both verticals (top and bottom), no horizontals */
#define KNIGHTRIDER_TRAIL_VALUE  CL3641BH8_MINUS  /**< dim flanking mark: reuse the "-" glyph (segment G only) */
#define KNIGHTRIDER_EYE_WIDTH    4U     /**< how many digits wide the bright eye is: "-||||-" */
/* Rightmost position the eye's left edge can take without any of its
 * digits spilling past the last one -- keeps the bright block itself
 * always fully on-screen (only the single-digit trailing dash on
 * either side can run off the edge, same as the original 1-wide eye). */
#define KNIGHTRIDER_MAX_ANCHOR   (CL3641BH8_NUM_DIGITS - KNIGHTRIDER_EYE_WIDTH)

/* digitBit[pos] = digit-select output bit that lights physical digit
 * "pos" (0 = leftmost ... 7 = rightmost). A cyclic rotation by 4 on
 * this unit (the two displays' bit groups are swapped relative to
 * their physical left/right placement) -- NOT sequential, do not
 * assume {0,1,2,...,7} on a different unit without recalibrating. */
static const uint8_t digitBit[CL3641BH8_NUM_DIGITS] = {4, 5, 6, 7, 0, 1, 2, 3};

/*===========================================================================*/
/* Driver state.                                                            */
/*===========================================================================*/

static uint8_t digitValue[CL3641BH8_NUM_DIGITS];
static bool    digitDp[CL3641BH8_NUM_DIGITS];
/* All off by default: nothing shows until a caller explicitly sets a
 * digit -- so cl3641bh8Start() never flashes a stray "00000000" before
 * the application has drawn anything real (e.g. main.c's tare
 * indicator, which takes over the very next scheduler tick). */
static bool    digitOn[CL3641BH8_NUM_DIGITS];

static SPIDriver *drvSpi;
static ioline_t   lineRck;

static const SPIConfig spicfg = {
  .circular         = FALSE,
  .slave            = FALSE,
  .data_cb          = NULL,
  .error_cb         = NULL,
  .ssport           = NULL,
  .sspad            = 0U,
  .cr1              = SPI_CR1_BR_1 | SPI_CR1_BR_0, /* f_PCLK/16 */
  .cr2              = SPI_CR2_DS_2 | SPI_CR2_DS_1 | SPI_CR2_DS_0 /* 8-bit */
};

/*===========================================================================*/
/* Multiplexing thread: refreshes one digit at a time, fast enough that     */
/* persistence of vision makes all 8 digits look lit simultaneously.        */
/* Working area is 512 bytes: a smaller 128-byte area silently overflowed   */
/* (no stack-check in this build) with an earlier bit-banged version of     */
/* this driver, freezing the display -- kept generous since it costs        */
/* nothing on a chip with 128 KB of RAM.                                    */
/*===========================================================================*/

static THD_WORKING_AREA(waCl3641bh8Mux, 512);
static THD_FUNCTION(thdCl3641bh8Mux, arg) {

  (void)arg;
  chRegSetThreadName("cl3641bh8-mux");

  unsigned ii = 0;
  while (true) {
    /* Snapshot this digit's state under lock: SetDigit/SetNumber(Dp)
     * write these arrays from another thread, and without this the mux
     * thread could read a torn mix of old/new values mid-update. */
    chSysLock();
    uint8_t value = digitValue[ii];
    bool    on    = digitOn[ii];
    bool    dp    = digitDp[ii];
    chSysUnlock();

    uint8_t segByte = 0x00;
    if (on) {
      if (value == CL3641BH8_MINUS) {
        segByte = MINUS_SEGBYTE;
      }
      else if (value == KNIGHTRIDER_EYE_VALUE) {
        segByte = KNIGHTRIDER_EYE_SEGBYTE;
      }
      else {
        segByte = font[value];
      }
    }
    if (on && dp) {
      segByte |= 0x80;
    }
    uint8_t digByte = (uint8_t)(1U << digitBit[ii]);

#if SEG_ACTIVE_LOW
    segByte = (uint8_t)~segByte;
#endif
#if DIGIT_ACTIVE_LOW
    digByte = (uint8_t)~digByte;
#endif

    /* far_byte=segByte, near_byte=digByte: on this unit the chip
     * cascaded farthest from the header drives segments, the one
     * wired directly to DIO/SCK/RCK drives digit-select. */
    hc595Write16(drvSpi, lineRck, segByte, digByte);

    ii = (ii + 1) % CL3641BH8_NUM_DIGITS;
    chThdSleepMicroseconds(1500); /* ~83 Hz full-frame refresh */
  }
}

/*===========================================================================*/
/* Public API.                                                              */
/*===========================================================================*/

void cl3641bh8Start(SPIDriver *spip, ioline_t sck_line,
                     ioline_t mosi_line, ioline_t rck_line) {

  drvSpi  = spip;
  lineRck = rck_line;

  palSetLineMode(sck_line, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(mosi_line, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(rck_line, PAL_MODE_OUTPUT_PUSHPULL |
                            PAL_STM32_OSPEED_HIGHEST);
  palClearLine(rck_line);

  spiStart(spip, &spicfg);

  chThdCreateStatic(waCl3641bh8Mux, sizeof(waCl3641bh8Mux),
                     NORMALPRIO + 1, thdCl3641bh8Mux, NULL);
}

void cl3641bh8SetDigit(unsigned pos, uint8_t value, bool dp) {

  if (pos >= CL3641BH8_NUM_DIGITS) {
    return;
  }
  chSysLock();
  if (value == CL3641BH8_BLANK) {
    digitOn[pos] = false;
  }
  else {
    digitOn[pos] = true;
    digitValue[pos] = (value == CL3641BH8_MINUS) ? value : (uint8_t)(value % 10U);
    digitDp[pos] = dp;
  }
  chSysUnlock();
}

void cl3641bh8SetNumber(uint32_t value) {

  uint32_t v = value % 100000000UL;
  chSysLock();
  for (int ii = CL3641BH8_NUM_DIGITS - 1; ii >= 0; ii--) {
    digitOn[ii] = true;
    digitValue[ii] = (uint8_t)(v % 10U);
    v /= 10U;
  }
  chSysUnlock();
}

void cl3641bh8SetNumberDp(uint32_t value, unsigned dpPos) {

  uint32_t v = value % 100000000UL;
  chSysLock();
  for (int ii = CL3641BH8_NUM_DIGITS - 1; ii >= 0; ii--) {
    digitOn[ii] = true;
    digitValue[ii] = (uint8_t)(v % 10U);
    digitDp[ii] = ((unsigned)ii == dpPos);
    v /= 10U;
  }
  chSysUnlock();
}

/**
 * @brief   Lights a KNIGHTRIDER_EYE_WIDTH-digit-wide "eye" whose left
 *          edge is @p eyeLeft, plus a single-digit "-" trailing mark
 *          immediately on either side ("-||||-" at the default width),
 *          and blanks every other digit, in one atomic update. Eye
 *          digits show both verticals (no horizontals, the brightest-
 *          looking glyph available); the trail marks show segment G
 *          only -- mimics KITT's soft-edged scanner without needing
 *          PWM brightness control. Whatever falls past either end of
 *          the strip is simply skipped (no wraparound); callers keep
 *          eyeLeft within [0, KNIGHTRIDER_MAX_ANCHOR] so only the
 *          1-digit trailing marks can ever be clipped, never the eye
 *          itself.
 * @note    Internal helper for cl3641bh8Knightrider() -- not exposed in
 *          the header, no other caller needs this glyph combination.
 */
static void cl3641bh8KnightriderStep(unsigned eyeLeft) {

  unsigned eyeRight = eyeLeft + KNIGHTRIDER_EYE_WIDTH - 1U;

  chSysLock();
  for (unsigned ii = 0; ii < CL3641BH8_NUM_DIGITS; ii++) {
    if (ii >= eyeLeft && ii <= eyeRight) {
      digitOn[ii]    = true;
      digitValue[ii] = KNIGHTRIDER_EYE_VALUE;
      digitDp[ii]    = false;
    }
    else if (ii + 1 == eyeLeft || ii == eyeRight + 1U) {
      digitOn[ii]    = true;
      digitValue[ii] = KNIGHTRIDER_TRAIL_VALUE;
      digitDp[ii]    = false;
    }
    else {
      digitOn[ii] = false;
    }
  }
  chSysUnlock();
}

/**
 * @brief   Runs one forward+backward sweep, checking *@p stopFlag (if
 *          not NULL) before every single step so a caller can cut the
 *          sweep short at step granularity, not just between sweeps.
 * @return  true if it stopped early because the flag was already set,
 *          false if it completed the whole sweep.
 */
static bool cl3641bh8KnightriderSweep(uint32_t stepDelayMs,
                                       const volatile bool *stopFlag) {

  /* Forward 0->KNIGHTRIDER_MAX_ANCHOR, then back to 1: neither end
   * repeats two steps in a row, so the eye doesn't visibly pause at
   * either edge. */
  for (unsigned anchor = 0; anchor <= KNIGHTRIDER_MAX_ANCHOR; anchor++) {
    if (stopFlag != NULL && *stopFlag) {
      return true;
    }
    cl3641bh8KnightriderStep(anchor);
    chThdSleepMilliseconds(stepDelayMs);
  }
  for (int anchor = (int)KNIGHTRIDER_MAX_ANCHOR - 1; anchor >= 1; anchor--) {
    if (stopFlag != NULL && *stopFlag) {
      return true;
    }
    cl3641bh8KnightriderStep((unsigned)anchor);
    chThdSleepMilliseconds(stepDelayMs);
  }
  return false;
}

/**
 * @brief   Shared core for cl3641bh8Knightrider() (fixed @p sweeps,
 *          pass stopFlag == NULL) and cl3641bh8KnightriderUntil() (runs
 *          until *stopFlag becomes true, pass sweeps == 0). Handles the
 *          snapshot-before/restore-after common to both.
 */
static void cl3641bh8KnightriderRun(unsigned sweeps, uint32_t stepDelayMs,
                                     const volatile bool *stopFlag) {

  /* Snapshot what's on screen now so it can be restored exactly once
   * the effect ends -- the caller doesn't need to know this ran, or
   * re-render anything afterwards; the display picks up right where
   * it left off. */
  uint8_t savedValue[CL3641BH8_NUM_DIGITS];
  bool    savedOn[CL3641BH8_NUM_DIGITS];
  bool    savedDp[CL3641BH8_NUM_DIGITS];

  chSysLock();
  for (unsigned ii = 0; ii < CL3641BH8_NUM_DIGITS; ii++) {
    savedValue[ii] = digitValue[ii];
    savedOn[ii]    = digitOn[ii];
    savedDp[ii]    = digitDp[ii];
  }
  chSysUnlock();

  if (stopFlag != NULL) {
    while (!(*stopFlag)) {
      if (cl3641bh8KnightriderSweep(stepDelayMs, stopFlag)) {
        break;
      }
    }
  }
  else {
    for (unsigned sweep = 0; sweep < sweeps; sweep++) {
      cl3641bh8KnightriderSweep(stepDelayMs, NULL);
    }
  }

  /* Restore exactly what was showing before the effect started. */
  chSysLock();
  for (unsigned ii = 0; ii < CL3641BH8_NUM_DIGITS; ii++) {
    digitValue[ii] = savedValue[ii];
    digitOn[ii]    = savedOn[ii];
    digitDp[ii]    = savedDp[ii];
  }
  chSysUnlock();
}

void cl3641bh8Knightrider(unsigned sweeps, uint32_t stepDelayMs) {

  cl3641bh8KnightriderRun(sweeps, stepDelayMs, NULL);
}

void cl3641bh8KnightriderUntil(const volatile bool *stopFlag,
                                uint32_t stepDelayMs) {

  cl3641bh8KnightriderRun(0U, stepDelayMs, stopFlag);
}
/** @} */
