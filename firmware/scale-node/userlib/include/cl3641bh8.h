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
 * @file    cl3641bh8.h
 * @brief   High-level driver for two CL3641BH 4-digit 7-segment displays
 *          multiplexed through the RobotDyn "8-Digit LED Display 74HC595"
 *          module (2x cascaded 74HC595 + on-board digit driver stage).
 *
 * Hides the shift-register protocol, segment/digit signal polarity, the
 * (non-sequential) digit bit-order and the multiplexing/refresh thread
 * behind a small numeric-display API. The polarity and bit-order values
 * baked into cl3641bh8.c were determined empirically on one physical
 * unit of this module -- see README.md at the project root for how, and
 * for how to redo that calibration on a different unit if needed.
 *
 * @{
 */

#ifndef _CL3641BH8_H_
#define _CL3641BH8_H_

#define CL3641BH8_NUM_DIGITS   8
#define CL3641BH8_BLANK        0xFFU  /**< value meaning "digit off" */
#define CL3641BH8_MINUS        0xFEU  /**< value meaning "show a minus sign" */

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief   Configures the SPI + latch pins, starts the SPI driver and
   *          launches the internal refresh (multiplexing) thread.
   * @note    Call once, after halInit()/chSysInit(). Blocking calls like
   *          cl3641bh8SetNumber() are safe to call from any thread
   *          afterwards; the display keeps refreshing in the background.
   * @note    All 8 digits start off (blank) -- nothing shows until a
   *          caller explicitly sets one.
   *
   * @param[in] spip       SPI driver to use (e.g. &SPID1)
   * @param[in] sck_line   PAL line wired to SCK, configured here as
   *                       PAL_MODE_ALTERNATE(5) (SPI1/2/3 AF on STM32G4)
   * @param[in] mosi_line  PAL line wired to DIO/MOSI, same AF as sck_line
   * @param[in] rck_line   PAL line wired to RCK (latch), plain GPIO
   */
  void cl3641bh8Start(SPIDriver *spip, ioline_t sck_line,
                       ioline_t mosi_line, ioline_t rck_line);

  /**
   * @brief   Sets one digit's value and decimal point.
   *
   * @param[in] pos    digit position, 0 = leftmost ... 7 = rightmost
   * @param[in] value  0..9, CL3641BH8_BLANK to turn the digit off, or
   *                   CL3641BH8_MINUS to show a minus sign there
   * @param[in] dp     true to also light that digit's decimal point
   */
  void cl3641bh8SetDigit(unsigned pos, uint8_t value, bool dp);

  /**
   * @brief   Writes an 8-digit number (0..99999999) across all digits,
   *          right-aligned with leading zeros, decimal points untouched
   *          (call cl3641bh8SetDigit() separately for a decimal point).
   */
  void cl3641bh8SetNumber(uint32_t value);

  /**
   * @brief   Writes an 8-digit number with a single decimal point turned
   *          on, e.g. for a fixed-point value: pass the digits as one
   *          integer (as if the point weren't there) plus where the
   *          point goes. Example: cl3641bh8SetNumberDp(12345678, 3)
   *          shows "1234.5678" (point after the 4th digit from the
   *          left, i.e. dpPos=3 since positions are 0-indexed).
   *
   * @param[in] value  the 8 digits, right-aligned with leading zeros
   * @param[in] dpPos  digit position whose decimal point to light,
   *                   0 = leftmost ... 7 = rightmost (any other value
   *                   turns all decimal points off)
   */
  void cl3641bh8SetNumberDp(uint32_t value, unsigned dpPos);

  /**
   * @brief   "Knight Rider" scanner effect: a KNIGHTRIDER_EYE_WIDTH
   *          (4)-digit-wide "eye" -- each of its digits lit as both
   *          verticals, no horizontals -- with a single "-" (segment G
   *          only, dimmer-looking) trailing mark on either side, i.e.
   *          "-||||-", sweeps left-to-right then right-to-left, for a
   *          given number of round trips.
   * @note    Blocking call: runs entirely on the calling thread (no
   *          new thread spawned), using the same locked digit-array
   *          updates as every other setter here, so it's safe to call
   *          from any thread already using this API. It blocks the
   *          caller for the whole effect duration (~sweeps *
   *          2*(8-4) * stepDelayMs ms with the default eye width --
   *          call it only when that thread has nothing more
   *          time-critical to do meanwhile (e.g. while idling between
   *          two readings), not from inside a tight real-time loop.
   * @note    Saves the digits/blanks/decimal-points on screen before
   *          starting and restores that exact state when done -- the
   *          caller does not need to re-render anything afterwards.
   *
   * @param[in] sweeps        number of full left-right-left passes
   * @param[in] stepDelayMs   delay between one digit and the next, in
   *                          ms -- lower is faster
   */
  void cl3641bh8Knightrider(unsigned sweeps, uint32_t stepDelayMs);

  /**
   * @brief   Same "-||||-" scanner effect as cl3641bh8Knightrider(),
   *          but instead of a fixed number of sweeps it repeats
   *          indefinitely until *@p stopFlag becomes true -- checked
   *          before every single step (not just between sweeps), so it
   *          stops as soon as the flag flips rather than finishing a
   *          whole extra sweep. Intended for "still working" indicators
   *          of unpredictable duration (e.g. the boot-time tare): set
   *          the flag, start this on its own thread, then clear the
   *          flag and chThdWait() that thread once the real work is
   *          done, before touching the display again.
   * @note    Same blocking/locking/save-restore guarantees as
   *          cl3641bh8Knightrider() -- see its docs above.
   *
   * @param[in] stopFlag      checked before each step; effect ends (and
   *                          the prior display state is restored) as
   *                          soon as it reads true
   * @param[in] stepDelayMs   delay between one digit and the next, in
   *                          ms -- lower is faster
   */
  void cl3641bh8KnightriderUntil(const volatile bool *stopFlag,
                                  uint32_t stepDelayMs);

#ifdef __cplusplus
}
#endif
#endif /* _CL3641BH8_H_ */

/** @} */
