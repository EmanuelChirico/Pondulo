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
 * @file    hc595.c
 * @brief   74HC595 (2x cascaded) hardware-SPI shift-register driver code.
 *
 * @addtogroup hc595
 * @{
 */

#include "ch.h"
#include "hal.h"

#include "hc595.h"

/**
 * @brief   Shifts two bytes into a chain of two cascaded 74HC595 and
 *          latches them to the outputs.
 * @pre     The SPI interface must be initialized and the driver started,
 *          the latch line must already be configured as push-pull output.
 *
 * @param[in] spip        pointer to the SPI interface
 * @param[in] rck_line    PAL line wired to RCK (latch/storage clock)
 * @param[in] far_byte    byte shifted first -> lands in the far chip
 * @param[in] near_byte   byte shifted second -> lands in the near chip
 */
void hc595Write16(SPIDriver *spip, ioline_t rck_line,
                   uint8_t far_byte, uint8_t near_byte) {

  uint8_t txbuf[2] = {far_byte, near_byte};

  spiSend(spip, 2, txbuf);
  palSetLine(rck_line);
  palClearLine(rck_line);
}
/** @} */
