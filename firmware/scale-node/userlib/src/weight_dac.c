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
 * @file    weight_dac.c
 * @brief   See weight_dac.h for the "why a real DAC" rationale.
 *
 * No thread needed here, unlike the PWM version it replaces: a DAC output
 * is just a register holding a voltage, so weightDacSetGrams() is a
 * complete, immediate operation on its own.
 */

#include "ch.h"
#include "hal.h"

#include "weight_dac.h"

#define WEIGHT_DAC_PIN_PORT   GPIOA
#define WEIGHT_DAC_PIN        4U   /* PA4 = DAC1_OUT1 on STM32G474 */

#define WEIGHT_DAC_FULL_SCALE 4095U /* 12-bit right-aligned */

static const DACConfig weight_dac_cfg = {
  .init     = 0U,
  .datamode = DAC_DHRM_12BIT_RIGHT,
  .cr       = 0U
};

void weightDacStart(void) {

  palSetPadMode(WEIGHT_DAC_PIN_PORT, WEIGHT_DAC_PIN, PAL_MODE_INPUT_ANALOG);
  dacStart(&DACD1, &weight_dac_cfg);
}

void weightDacSetGrams(uint16_t grams) {

  if (grams > WEIGHT_DAC_MAX_GRAMS) {
    grams = (uint16_t)WEIGHT_DAC_MAX_GRAMS;
  }

  uint32_t code = ((uint32_t)grams * WEIGHT_DAC_FULL_SCALE) /
                   WEIGHT_DAC_MAX_GRAMS;
  dacPutChannelX(&DACD1, 0U, (dacsample_t)code);
}
/** @} */
