/*
 * gesture_link - framing protocol for the ToF-node -> display-node gesture
 * link, plus an incremental byte-fed parser and an encoder.
 *
 * Frame layout (from gruppo6/CLAUDE.md):
 *   [START=0xAA][TIPO][LEN][PAYLOAD...][CHECKSUM]
 *   TIPO = 0x01 (MSG_TYPE_GESTURE), PAYLOAD = msg_gesture_t, CHECKSUM = XOR
 *   of every payload byte.
 *
 * The parser only depends on being fed one byte at a time via
 * gestureLinkParserFeed(); it does not care whether those bytes come from a
 * real UART RX thread or a software self-test loop, so the two can be
 * swapped without touching this file.
 */

#ifndef GESTURE_LINK_H
#define GESTURE_LINK_H

#include <stdint.h>
#include <stdbool.h>

#define GESTURE_LINK_FRAME_START   0xAAU
#define GESTURE_LINK_MSG_GESTURE   0x01U

/* Mirrors gesture_event_t from gruppo6/CLAUDE.md - payload byte values must
 * stay in this order to match what the ToF node sends. */
typedef enum {
  GESTURE_NONE = 0,
  GESTURE_SWIPE_LEFT,
  GESTURE_SWIPE_RIGHT,
  GESTURE_SWIPE_UP,
  GESTURE_SWIPE_DOWN,
  GESTURE_PUSH,
  GESTURE_HOLD,
  /* PRESENCE (2026-09-04): rilevamento presenza a lungo raggio (~50cm) sul
   * sensore ToF, indipendente dalla zona di puntamento - id=7 nel
   * protocollo $GS, vedi cursorSensor/keypad/gesture.h GESTURE_PRESENCE. */
  GESTURE_PRESENCE
} gesture_event_t;

typedef struct {
  uint8_t gesto;
} __attribute__((packed)) msg_gesture_t;

typedef enum {
  GL_WAIT_START = 0,
  GL_WAIT_TYPE,
  GL_WAIT_LEN,
  GL_WAIT_PAYLOAD,
  GL_WAIT_CHECKSUM
} gesture_link_state_t;

typedef struct {
  gesture_link_state_t state;
  uint8_t type;
  uint8_t len;
  uint8_t payload[8];   /* only msg_gesture_t (1 byte) is used today, headroom for future payload types */
  uint8_t payload_idx;
  uint8_t checksum;
} gesture_link_parser_t;

void gestureLinkParserInit(gesture_link_parser_t *p);

/*
 * Feeds one received byte into the parser. Returns true and writes the
 * decoded gesture into *out when a complete, checksum-valid
 * MSG_TYPE_GESTURE frame has just been assembled. A bad checksum, an
 * unrecognized type, or an out-of-range gesture byte silently resets the
 * parser to GL_WAIT_START (malformed frames are dropped, not reported -
 * the link has no NACK/retransmit).
 */
bool gestureLinkParserFeed(gesture_link_parser_t *p, uint8_t byte, gesture_event_t *out);

/*
 * Encodes a single-gesture frame into buf (caller-owned, must be >= 5
 * bytes). Returns the number of bytes written. Used by the self-test
 * injector today, and available for any future sender on this side of the
 * link.
 */
uint8_t gestureLinkEncode(gesture_event_t g, uint8_t *buf);

#endif /* GESTURE_LINK_H */
