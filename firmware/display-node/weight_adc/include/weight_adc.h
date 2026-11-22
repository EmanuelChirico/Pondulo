/*
 * weight_adc - true analog voltage receiver for the weight value coming
 * from the separate scale board (its own Nucleo-G474RE, load cell + HX711
 * + CL3641BH 7-segment display, see the "bilanciaFinale" project).
 *
 * REPLACES weight_pwm (2026-09-05): the PWM link's decoded value jittered
 * noticeably even at a physically stable weight, and the redraw it drove on
 * the Bilancia screen was reported as very distracting - on top of an
 * earlier serial link that failed outright once the real ToF sensor board
 * shared this board's USART3 (see weight_pwm.h's history in git/REPORT.md
 * for that chain of fixes, now moot). Speed was never a requirement here -
 * the scale only needs to be read while the Bilancia screen is up - so a
 * plain DC voltage read by the ADC removes every timing concern (bit
 * sampling, edge jitter, PWM period) at once: bilanciaFinale's DAC just
 * sits at whatever voltage corresponds to the current weight, and this
 * side reads it with a single blocking ADC conversion whenever it wants a
 * fresh value - nothing to synchronize with, nothing that can desync.
 *
 * Formula (must match bilanciaFinale's weight_dac.c exactly):
 *   grams 0..WEIGHT_ADC_MAX_GRAMS  <->  DAC/ADC code 0..4095, linear.
 *
 * Pin: PA4 ("A2" on the Nucleo Arduino header) - moved here from PC1/"A4"
 * (2026-09-05, during the PWM iteration) specifically to sit away from
 * this board's own PC10/PC11 ToF wiring; kept on PA4 for this analog
 * version since PA4 conveniently has both an ADC2 input (channel 17, used
 * here) and, on bilanciaFinale's side, a DAC output on the very same pin
 * number - no further pin research needed there.
 */

#ifndef WEIGHT_ADC_H
#define WEIGHT_ADC_H

#include <stdint.h>
#include "hal.h"

/* Must match bilanciaFinale's WEIGHT_DAC_MAX_GRAMS (weight_dac.h there). */
#define WEIGHT_ADC_MAX_GRAMS 1000U

#ifdef __cplusplus
extern "C" {
#endif

/* Configures PA4 as analog input and starts ADCD2 (channel 17). Call once
 * after halInit()/chSysInit(). */
void weight_adc_init(void);

/* Blocking one-shot ADC conversion, decoded to grams (0..WEIGHT_ADC_MAX_GRAMS,
 * clamped). Takes on the order of a few microseconds - cheap enough to call
 * every UI tick without worrying about it, per the "speed doesn't matter,
 * just needs to read during the Bilancia screen" requirement. */
uint16_t weight_adc_get_grams(void);

/* Diagnostics: last raw 12-bit ADC code (0..4095), for verifying the link
 * is actually responding to the scale's DAC output rather than reading a
 * stuck/floating value. */
uint16_t weight_adc_get_raw_sample(void);

#ifdef __cplusplus
}
#endif

#endif /* WEIGHT_ADC_H */
