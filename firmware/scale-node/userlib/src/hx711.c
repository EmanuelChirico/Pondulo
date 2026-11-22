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
 * @file    hx711.c
 * @brief   Bit-banged driver for the HX711 24-bit ADC / load-cell amplifier.
 *          See hx711.h for the protocol summary; HX711 datasheet for the
 *          full timing spec (SCK high pulse must stay under 60us or the
 *          chip powers down).
 *
 * @addtogroup hx711
 * @{
 */

#include "ch.h"
#include "hal.h"

#include "hx711.h"

static ioline_t lineDt;
static ioline_t lineSck;

/* A handful of NOPs is enough margin above the HX711's minimum SCK
 * pulse width (~0.2us) at this MCU's clock speed, without needing a
 * calibrated microsecond delay. */
static inline void hx711PulseDelay(void) {

  for (volatile int ii = 0; ii < 8; ii++) {
    __NOP();
  }
}

void hx711Start(ioline_t dt_line, ioline_t sck_line) {

  lineDt  = dt_line;
  lineSck = sck_line;

  /* Weak pull-up (2026-09-05): DT is actively driven push-pull by the HX711
   * in normal operation, so this pull is overridden and invisible most of
   * the time - it only matters if the line is ever briefly tri-stated or
   * disconnected, in which case it reads back a defined HIGH ("not ready")
   * instead of floating/reading a false LOW ("ready", with garbage data
   * behind it). */
  palSetLineMode(lineDt, PAL_MODE_INPUT_PULLUP);
  palSetLineMode(lineSck, PAL_MODE_OUTPUT_PUSHPULL);
  palClearLine(lineSck);
}

bool hx711IsReady(void) {

  return palReadLine(lineDt) == PAL_LOW;
}

/* Worst-case HX711 conversion time is ~100ms at the slower 10SPS rate
 * setting; this is a generous multiple of that. Below this fix (2026-09-05)
 * hx711Read() waited here with NO timeout at all - since this project's
 * main() is single-threaded (no watchdog either, see cfg/halconf.h/
 * mcuconf.h), a single stuck/noisy conversion (or the chip briefly not
 * responding) froze the ENTIRE scale board forever: no more reads, no more
 * weightLinkSend() to DEMO6 - which on the DEMO6 side looked exactly like
 * "the weight stopped updating", both on its diagnostic serial output and
 * on the Bilancia screen, because both simply display the last value
 * DEMO6 ever received and nothing new was ever sent again. */
#define HX711_READY_TIMEOUT_US  300000U
#define HX711_POLL_STEP_US      100U

int32_t hx711Read(void) {

  uint32_t waited_us = 0U;
  while (palReadLine(lineDt) == PAL_HIGH) {
    if (waited_us >= HX711_READY_TIMEOUT_US) {
      return HX711_TIMEOUT; /* give up - never block the caller forever */
    }
    chThdSleepMicroseconds(HX711_POLL_STEP_US);
    waited_us += HX711_POLL_STEP_US;
  }

  uint32_t raw = 0;

  /* The pulse train below is timing-critical: the HX711 powers down if
   * SCK stays high for more than ~60us. Lock out preemption for its
   * duration so a context switch (e.g. the display refresh thread
   * waking up) can't stretch a high pulse past that threshold and
   * strand hx711Read() waiting forever on a powered-down chip. */
  chSysLock();
  for (unsigned ii = 0; ii < 24U; ii++) {
    palSetLine(lineSck);
    hx711PulseDelay();
    raw = (raw << 1) | (palReadLine(lineDt) == PAL_HIGH ? 1U : 0U);
    palClearLine(lineSck);
    hx711PulseDelay();
  }

  /* 25th pulse: selects channel A, gain 128 for the *next* conversion
   * (this is the HX711's default/most common config for a load cell). */
  palSetLine(lineSck);
  hx711PulseDelay();
  palClearLine(lineSck);
  hx711PulseDelay();
  chSysUnlock();

  /* Sign-extend the 24-bit two's-complement result to 32 bits. */
  if ((raw & 0x00800000UL) != 0U) {
    raw |= 0xFF000000UL;
  }
  return (int32_t)raw;
}

void hx711PowerDown(void) {

  palClearLine(lineSck);
  palSetLine(lineSck);
}

void hx711PowerUp(void) {

  palClearLine(lineSck);
}
/** @} */
