#include "ch.h"
#include "hal.h"
#include "price_link.h"
#include <string.h>
static mutex_t price_mutex;
static uint32_t latest[PRICE_COUNT];
static bool dirty;
static const SerialConfig price_serial_cfg = {38400, 0, USART_CR2_STOP1_BITS, 0};
static THD_WORKING_AREA(waPriceReceiver, 768);
static THD_FUNCTION(PriceReceiver, arg) {
  (void)arg;
  chRegSetThreadName("price_rx");
  price_parser_t parser;
  uint32_t prices[PRICE_COUNT];
  price_parser_init(&parser);
  while (true) {
    msg_t b = sdGetTimeout(&SD1, TIME_MS2I(100));
    if (b < MSG_OK) continue;
    if (price_parser_feed(&parser, (char)b,
                          (uint32_t)TIME_I2MS(chVTGetSystemTimeX()), prices)) {
      chMtxLock(&price_mutex);
      memcpy(latest, prices, sizeof(latest));
      dirty = true;
      chMtxUnlock(&price_mutex);
    }
  }
}
void price_link_start(void) {
  chMtxObjectInit(&price_mutex);
  dirty = false;
  /* RX only: do not claim PA9 (LCD CS) or PC4 unnecessarily. */
  palSetPadMode(GPIOC, 5U, PAL_MODE_ALTERNATE(7) | PAL_STM32_PUPDR_PULLUP);
  sdStart(&SD1, &price_serial_cfg);
  chThdCreateStatic(waPriceReceiver, sizeof(waPriceReceiver),
                   NORMALPRIO + 1, PriceReceiver, NULL);
}
bool price_link_take(uint32_t out[PRICE_COUNT]) {
  bool available;
  chMtxLock(&price_mutex);
  available = dirty;
  if (available) { memcpy(out, latest, sizeof(latest)); dirty = false; }
  chMtxUnlock(&price_mutex);
  return available;
}
