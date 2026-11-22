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
 * @file    hc595.h
 * @brief   74HC595 (2x cascaded) hardware-SPI shift-register driver header.
 *
 * @{
 */

#ifndef _HC595_H_
#define _HC595_H_

/*===========================================================================*/
/* Driver pre-compile time settings.                                        */
/*===========================================================================*/

#if !HAL_USE_SPI
#error "HC595 requires HAL_USE_SPI"
#endif

/*===========================================================================*/
/* External declarations.                                                   */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief   Shifts two bytes into a chain of two cascaded 74HC595 (over
   *          hardware SPI, MOSI+SCK) and latches them to the outputs.
   * @note    The byte shifted out FIRST ends up, after the second byte is
   *          shifted, in the chip FARTHEST from the MCU (the second board
   *          in the chain); the byte shifted out LAST stays in the chip
   *          NEAREST the MCU (the first board, wired to DIO/SCK/RCK).
   *
   * @param[in] spip        pointer to the SPI interface (DIO=MOSI, SCK=SCK)
   * @param[in] rck_line    PAL line wired to RCK (latch/storage clock)
   * @param[in] far_byte    byte shifted first -> lands in the far chip
   * @param[in] near_byte   byte shifted second -> lands in the near chip
   */
  void hc595Write16(SPIDriver *spip, ioline_t rck_line,
                     uint8_t far_byte, uint8_t near_byte);

#ifdef __cplusplus
}
#endif
#endif /* _HC595_H_ */

/** @} */
