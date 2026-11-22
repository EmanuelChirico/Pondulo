/*
 * pointer_link - receiver for the ToF-node -> display-node continuous
 * pointer stream, ported from cursorDisplay/display/xy_link.{h,c}.
 *
 * Protocol (matches cursorSensor/keypad/keypad_link.c byte-for-byte):
 *   ASCII line on SD3 @ 921600 8N1: "$XY,<seq>,<valid>,<x>,<y>\r\n"
 *   seq   = uint32, monotonically increasing
 *   valid = 0 or 1 (whether a hand is currently in range)
 *   x     = 0..319 (screen pixel column)
 *   y     = 0..239 (screen pixel row)
 *
 * Unlike the cursorDisplay original, xy_link_init() here does NOT touch
 * hardware (no sdStart/palSetPadMode) - this project's convention is that
 * main.c owns all peripheral bring-up. Call xy_link_init() any time after
 * main.c has called sdStart(&SD3, ...) at 921600 baud.
 */

#ifndef XY_LINK_H
#define XY_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "ch.h"
#include "hal.h"
#include "gesture_link.h"

#define XY_LINE_BUF_SIZE   128U

typedef struct {
  uint32_t seq;
  uint8_t  valid; /* 0 or 1 */
  uint16_t x;     /* 0..319 */
  uint16_t y;     /* 0..239 */
} xy_msg_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Resets internal parser/state only (mutex, buffers, counters). Does not
 * touch SD3 or GPIOs - main.c is responsible for sdStart(&SD3, ...) and the
 * PC10/PC11 AF7 pad setup before this is called. */
void xy_link_init(void);

/*
 * Reads bytes from SD3 with timeout, buffers until newline, parses $XY frame.
 * Returns true if a complete, valid frame was received and published.
 */
bool xy_link_poll(sysinterval_t timeout);

/* Thread-safe retrieval of latest parsed raw message (for the UI/renderer) */
void xy_link_get_last_msg(xy_msg_t *out_msg);

uint32_t xy_link_get_frame_count(void);
uint32_t xy_link_get_raw_byte_count(void);

/* OR-accumulated (never cleared internally) SD3 channel error flags seen
 * since xy_link_init() - CHN_OVERRUN_ERROR / CHN_FRAMING_ERROR /
 * CHN_NOISE_ERROR / CHN_PARITY_ERROR / CHN_BUFFER_FULL_ERROR (see
 * hal_channels.h). Diagnostic for "bytes arrive but never parse": if this
 * is nonzero the UART/queue itself is the problem, not xy_link's parser. */
uint32_t xy_link_get_sticky_errors(void);

/*
 * Copies up to max_len of the most recently received raw bytes (regardless
 * of line framing/parse success) into out[], oldest first. Returns the
 * number of bytes actually copied. Pure diagnostic: lets you see literally
 * what's arriving on the wire instead of inferring it from the counters.
 */
uint8_t xy_link_get_raw_dump(uint8_t *out, uint8_t max_len);

/*
 * Pure string parser conforming strictly to the $XY protocol:
 * Format: $XY,<seq>,<valid>,<x>,<y>\r\n
 * Validates:
 *   prefix "$XY,"
 *   seq (uint32)
 *   valid (0 or 1)
 *   x (< 320)
 *   y (< 240)
 */
bool xy_link_parse_line(const char *line, xy_msg_t *out_msg);

/*
 * Pure string parser for the $GS gesture protocol:
 * Format: $GS,<seq>,<gesture_id>\r\n
 * Validates:
 *   prefix "$GS,"
 *   seq (uint32)
 *   gesture_id: only IDs 1..4 (SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN)
 *   are mapped to out_g and return true.
 *   IDs 0, 5, 6 (ENTER, EXIT on sensor), or out-of-range return false and GESTURE_NONE.
 */
bool xy_link_parse_gesture_line(const char *line, gesture_event_t *out_g);

/*
 * Sets the target mailbox for real-time gesture events parsed from $GS stream.
 * Must be called before PointerLinkThread starts reading from SD3.
 */
void xy_link_set_gesture_mailbox(mailbox_t *mb);

#ifdef __cplusplus
}
#endif

#endif /* XY_LINK_H */
