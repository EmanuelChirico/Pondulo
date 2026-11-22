#ifndef PRICE_LINK_H
#define PRICE_LINK_H
#include "price_parser.h"
/* Initializes SD1 RX PC5 AF7, 38400 8N1 and starts its dedicated RX thread.
 * Call once after halInit/chSysInit. No UI calls occur in the RX thread. */
void price_link_start(void);
/* Latest complete list, consumed once. Safe across threads. */
bool price_link_take(uint32_t out[PRICE_COUNT]);
#endif
