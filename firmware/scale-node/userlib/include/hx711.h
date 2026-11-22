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
 * @file    hx711.h
 * @brief   Bit-banged driver for the HX711 24-bit ADC / load-cell amplifier.
 *
 * @{
 */

#ifndef _HX711_H_
#define _HX711_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief   Sentinel returned by hx711Read() when the HX711 never asserted
   *          DT (ready) within HX711_READY_TIMEOUT_US (see hx711.c). Chosen
   *          outside the real 24-bit signed range (-8388608..8388607), so it
   *          can never be confused with a genuine reading - callers must
   *          check for it before using the result (see hx711ReadFiltered()
   *          in main.c for the reference pattern: skip the sample, keep the
   *          existing filtered average instead of blocking or corrupting
   *          it).
   */
#define HX711_TIMEOUT INT32_MIN

  /**
   * @brief   Configures the DT (data) and SCK (clock) GPIO lines.
   * @note    Call once, after halInit()/chSysInit().
   *
   * @param[in] dt_line    PAL line wired to DT/DOUT, configured as input
   *                       with weak pull-up (guards against a transient
   *                       tri-stated/disconnected line reading as a false
   *                       "ready", see hx711Start())
   * @param[in] sck_line   PAL line wired to SCK, configured as push-pull
   *                       output, idle low
   */
  void hx711Start(ioline_t dt_line, ioline_t sck_line);

  /**
   * @brief   True when a conversion result is ready to be read (DT low).
   */
  bool hx711IsReady(void);

  /**
   * @brief   Waits for a conversion to be ready, then reads it: 24 clock
   *          pulses shift out the signed 24-bit result MSB-first, a 25th
   *          pulse selects channel A / gain 128 for the next conversion.
   * @return  Signed, sign-extended raw ADC reading (-8388608..8388607), or
   *          HX711_TIMEOUT if DT never went low within the timeout (chip
   *          not responding - noise, bad contact, brief brownout) - callers
   *          MUST check for this instead of assuming every call succeeds.
   */
  int32_t hx711Read(void);

  /**
   * @brief   Puts the HX711 in power-down mode (SCK held high >60us).
   */
  void hx711PowerDown(void);

  /**
   * @brief   Wakes the HX711 from power-down mode (SCK low).
   */
  void hx711PowerUp(void);

#ifdef __cplusplus
}
#endif
#endif /* _HX711_H_ */

/** @} */
