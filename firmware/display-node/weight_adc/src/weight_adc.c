/*
 * weight_adc - see weight_adc.h for the "why analog voltage" rationale.
 *
 * One-shot, software-triggered, single-channel conversion - the simplest
 * possible ADC usage pattern, matching the "speed doesn't matter" brief.
 * No DMA, no continuous/circular group, no callback: weight_adc_get_grams()
 * just blocks (a few microseconds) on adcConvert() and returns.
 *
 * EMA FILTER (2026-09-06, DEMO7 experiment - see docs/REPORT.md): a single
 * raw ADC sample over an unshielded breadboard jumper has enough noise to
 * cross the Bilancia screen's redraw deadband almost every ~20ms tick,
 * which is what drove the reported continuous flicker. Three independent
 * code-review agents converged on the same fix: filter in software rather
 * than repeat the earlier oversampling attempt (which added more/slower
 * ADC calls and coincided with, but was not found to plausibly cause, a
 * separate pointer-visibility incident - see REPORT.md for the full
 * writeup). An exponential moving average applied to the EXISTING single
 * conversion per tick adds no extra ADC calls, no extra blocking time, and
 * touches nothing outside this file - it cannot affect pointer_link/xy_link
 * timing or thread priorities in any way. weight_adc_get_raw_sample()
 * still returns the true unfiltered sample (not the EMA), specifically so
 * the existing diagnostic printf in main.c can show raw vs. filtered side
 * by side when checking this on real hardware. */
#include "weight_adc.h"

#define WEIGHT_ADC_PIN_PORT   GPIOA
#define WEIGHT_ADC_PIN        4U   /* PA4 = ADC2_IN17 on STM32G474 */

#define WEIGHT_ADC_FULL_SCALE 4095U /* 12-bit right-aligned */

/* alpha = 1/8 (shift-based EMA, no floating point): a real weight change
 * reaches ~90% of its new value within about 18 ticks (~360ms at 20ms/
 * tick) - imperceptible for a kitchen-scale-style readout - while a single
 * noisy sample only nudges the filtered value by 1/8 of its own deviation,
 * keeping it well under the 3g redraw deadband in the steady state. */
#define WEIGHT_ADC_EMA_SHIFT 3U

static adcsample_t sample;        /* true raw sample, for diagnostics */
static int32_t     ema_sample;    /* filtered sample, drives grams output */
static bool         ema_ready = false;

static const ADCConfig weight_adc_cfg = {
  .difsel = 0U
};

/* One shot, 1 channel, software triggered - see the official ChibiOS
 * STM32G474RE-Nucleo64 ADC testhal (testhal/STM32/multi/ADC/cfg/
 * stm32g474re_nucleo64/portab.c) for the reference config shape this
 * mirrors. */
static const ADCConversionGroup weight_adc_grpcfg = {
  .circular     = false,
  .num_channels = 1U,
  .end_cb       = NULL,
  .error_cb     = NULL,
  .cfgr         = 0U,
  .cfgr2        = 0U,
  .tr1          = ADC_TR_DISABLED,
  .tr2          = ADC_TR_DISABLED,
  .tr3          = ADC_TR_DISABLED,
  .awd2cr       = 0U,
  .awd3cr       = 0U,
  .smpr         = {
    0U,
    ADC_SMPR2_SMP_AN17(ADC_SMPR_SMP_247P5)
  },
  .sqr          = {
    ADC_SQR1_SQ1_N(ADC_CHANNEL_IN17),
    0U,
    0U,
    0U
  }
};

void weight_adc_init(void) {
  palSetPadMode(WEIGHT_ADC_PIN_PORT, WEIGHT_ADC_PIN, PAL_MODE_INPUT_ANALOG);
  sample = 0U;
  ema_sample = 0;
  ema_ready = false;
  adcStart(&ADCD2, &weight_adc_cfg);
}

uint16_t weight_adc_get_grams(void) {
  adcConvert(&ADCD2, &weight_adc_grpcfg, &sample, 1U);

  if (!ema_ready) {
    /* First reading ever: seed the filter directly instead of easing into
     * it over ~18 ticks, so the display doesn't start at a stale 0 while
     * the filter "warms up". */
    ema_sample = (int32_t)sample;
    ema_ready = true;
  }
  else {
    ema_sample += ((int32_t)sample - ema_sample) >> WEIGHT_ADC_EMA_SHIFT;
  }

  uint32_t grams = ((uint32_t)ema_sample * WEIGHT_ADC_MAX_GRAMS) /
                    WEIGHT_ADC_FULL_SCALE;
  return (grams > WEIGHT_ADC_MAX_GRAMS) ? (uint16_t)WEIGHT_ADC_MAX_GRAMS
                                        : (uint16_t)grams;
}

uint16_t weight_adc_get_raw_sample(void) {
  return (uint16_t)sample;
}
