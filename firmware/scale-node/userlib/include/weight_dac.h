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
 * @file    weight_dac.h
 * @brief   True analog voltage transmitter for the weight value sent to
 *          DEMO6, over the STM32's own hardware DAC.
 *
 * REPLACES weight_pwm (2026-09-05): see DEMO6's weight_adc.h for the full
 * rationale (jittery decoded readings + constant screen redraws with PWM,
 * on top of an earlier serial link that failed outright once the real ToF
 * board shared DEMO6's USART3). A real DAC output has none of PWM's
 * decode-timing concerns - it just sits at a DC voltage, so DEMO6 reads a
 * clean, stable value with a plain ADC conversion, no edge timestamps or
 * ratios involved at all.
 *
 * Pin: PA4 (DAC1_OUT1) -> DEMO6 PA4 (ADC2_IN17), GND->GND. Formula (must
 * match DEMO6's WEIGHT_ADC_MAX_GRAMS exactly):
 *   grams 0..WEIGHT_DAC_MAX_GRAMS  <->  DAC code 0..4095, linear.
 */

#ifndef WEIGHT_DAC_H
#define WEIGHT_DAC_H

#include <stdint.h>
#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief   Full-scale value the DAC code formula maps to 4095 (~VDDA).
   * @note    Must match DEMO6's WEIGHT_ADC_MAX_GRAMS (weight_adc.h there).
   */
#define WEIGHT_DAC_MAX_GRAMS 1000U

  /**
   * @brief   Configures PA4 as analog output and starts DACD1 (channel 1)
   *          at 0V (0g) - matches "always 0g until a real weight arrives".
   * @note    Call once, after halInit()/chSysInit().
   */
  void weightDacStart(void);

  /**
   * @brief   Sets the output voltage to represent the given weight.
   * @note    Clamped to 0..WEIGHT_DAC_MAX_GRAMS. Takes effect immediately -
   *          a DAC write is a plain register write, no waveform/thread
   *          involved (unlike the PWM version this replaces).
   *
   * @param[in] grams  weight in whole grams
   */
  void weightDacSetGrams(uint16_t grams);

#ifdef __cplusplus
}
#endif
#endif /* WEIGHT_DAC_H */

/** @} */
