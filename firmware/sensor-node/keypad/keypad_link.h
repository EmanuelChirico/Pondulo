#ifndef KEYPAD_LINK_H
#define KEYPAD_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "ch.h"
#include "hal.h"
#include "gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the serial link stream for transmitting $XY and $GS frames.
 * If stream is NULL, defaults to (BaseSequentialStream *)&SD3.
 */
void keypad_link_init(BaseSequentialStream *stream);

/*
 * Checks whether the current cursor state is dirty compared to the
 * last transmitted packet (or if ~500ms heartbeat timeout expired).
 * If dirty, serializes and transmits:
 *   $XY,<seq>,<valid>,<x>,<y>\r\n
 * over the configured stream.
 */
void keypad_link_send_if_dirty(bool valid, uint16_t x, uint16_t y, systime_t now);

/*
 * ============================================================================
 * Protocollo Gesture: $GS,<seq_gesture>,<gesture_id>\r\n
 *
 * Canale seriale: SD3 (PC10 TX / PC11 RX @ 921600 baud, 8N1).
 * Flusso parallelo e additivo rispetto al flusso continuo $XY,...
 *
 * Campi:
 *   $GS             Prefisso fisso riga gesture
 *   <seq_gesture>   Contatore di sequenza monotono indipendente da $XY
 *   <gesture_id>    Identificativo numerico dell'evento (gesture_event_t):
 *                     1: GESTURE_SWIPE_LEFT
 *                     2: GESTURE_SWIPE_RIGHT
 *                     3: GESTURE_SWIPE_UP
 *                     4: GESTURE_SWIPE_DOWN
 *                     5: GESTURE_ENTER (prossimita' vicina < 90mm)
 *                     6: GESTURE_EXIT  (allontanamento > 190mm)
 *
 * Frequenza: al massimo 1 riga per gesto confermato (separato da GESTURE_COOLDOWN_MS).
 * Non invia nulla se nessun gesto viene confermato.
 * ============================================================================
 */
void keypad_link_send_gesture(gesture_event_t g, systime_t now);

#ifdef __cplusplus
}
#endif

#endif /* KEYPAD_LINK_H */
