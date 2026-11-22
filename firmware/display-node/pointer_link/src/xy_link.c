#include "xy_link.h"
#include <stdlib.h>
#include <string.h>

static char rx_line_buffer[XY_LINE_BUF_SIZE];
static size_t rx_line_idx = 0;
static volatile uint32_t raw_byte_count = 0;

#define XY_RAW_DUMP_LEN 24U
static uint8_t raw_dump[XY_RAW_DUMP_LEN];
static uint8_t raw_dump_count = 0;
static uint8_t raw_dump_head = 0;

static xy_msg_t current_msg;
static uint32_t frame_count = 0;
static mutex_t link_mutex;

static event_listener_t sd3_el;
static eventflags_t sticky_errors = 0;

void xy_link_init(void) {
  chMtxObjectInit(&link_mutex);
  memset(&current_msg, 0, sizeof(current_msg));
  frame_count = 0;
  raw_byte_count = 0;
  rx_line_idx = 0;
  raw_dump_count = 0;
  raw_dump_head = 0;
  sticky_errors = 0;
  chEvtRegister(chnGetEventSource(&SD3), &sd3_el, 0);
}

uint32_t xy_link_get_sticky_errors(void) {
  sticky_errors |= chEvtGetAndClearFlags(&sd3_el);
  return (uint32_t)sticky_errors;
}

bool xy_link_parse_line(const char *line, xy_msg_t *out_msg) {
  if ((line == NULL) || (out_msg == NULL)) {
    return false;
  }

  /* Protocol rule: Line must start with "$XY," */
  const char prefix[] = "$XY,";
  size_t prefix_len = 4U;
  if (strncmp(line, prefix, prefix_len) != 0) {
    return false;
  }

  const char *p = line + prefix_len;
  char *endp = NULL;

  /* Field 1: seq */
  unsigned long seq = strtoul(p, &endp, 10);
  if ((endp == p) || (*endp != ',')) {
    return false;
  }
  p = endp + 1;

  /* Field 2: valid (0 or 1) */
  unsigned long valid = strtoul(p, &endp, 10);
  if ((endp == p) || (*endp != ',')) {
    return false;
  }
  if (valid > 1UL) {
    return false;
  }
  p = endp + 1;

  /* Field 3: x (0..319) */
  unsigned long x = strtoul(p, &endp, 10);
  if ((endp == p) || (*endp != ',')) {
    return false;
  }
  if (x >= 320UL) {
    return false;
  }
  p = endp + 1;

  /* Field 4: y (0..239) */
  unsigned long y = strtoul(p, &endp, 10);
  if (endp == p) {
    return false;
  }
  if (y >= 240UL) {
    return false;
  }

  /* Trailing characters must only be \r, \n or \0 */
  const char *tail = endp;
  while ((*tail == '\r') || (*tail == '\n')) {
    tail++;
  }
  if (*tail != '\0') {
    return false;
  }

  out_msg->seq = (uint32_t)seq;
  out_msg->valid = (uint8_t)valid;
  out_msg->x = (uint16_t)x;
  out_msg->y = (uint16_t)y;

  return true;
}

static mailbox_t *gesture_mailbox_ptr = NULL;

void xy_link_set_gesture_mailbox(mailbox_t *mb) {
  gesture_mailbox_ptr = mb;
}

bool xy_link_parse_gesture_line(const char *line, gesture_event_t *out_g) {
  if ((line == NULL) || (out_g == NULL)) {
    return false;
  }

  /* Protocol rule: Line must start with "$GS," */
  const char prefix[] = "$GS,";
  size_t prefix_len = 4U;
  if (strncmp(line, prefix, prefix_len) != 0) {
    return false;
  }

  const char *p = line + prefix_len;
  char *endp = NULL;

  /* Field 1: seq */
  unsigned long seq = strtoul(p, &endp, 10);
  (void)seq;
  if ((endp == p) || (*endp != ',')) {
    return false;
  }
  p = endp + 1;

  /* Field 2: gesture_id */
  unsigned long gesture_id = strtoul(p, &endp, 10);
  if (endp == p) {
    return false;
  }

  /* Trailing characters must only be \r, \n or \0 */
  const char *tail = endp;
  while ((*tail == '\r') || (*tail == '\n')) {
    tail++;
  }
  if (*tail != '\0') {
    return false;
  }

  /* Map swipe events 1..4 (SWIPE_LEFT..SWIPE_DOWN), 5 (ENTER -> GESTURE_PUSH)
   * and 7 (PRESENCE -> GESTURE_PRESENCE, welcome sound). ID 6 (EXIT on
   * cursorSensor) is valid protocol syntax but deliberately excluded from
   * gestureMailbox (returns false). */
  if (((gesture_id >= 1UL) && (gesture_id <= 5UL)) || (gesture_id == 7UL)) {
    *out_g = (gesture_event_t)gesture_id;
    return true;
  }

  *out_g = GESTURE_NONE;
  return false;
}

bool xy_link_poll(sysinterval_t timeout) {
  msg_t ch = sdGetTimeout(&SD3, timeout);
  if (ch < MSG_OK) {
    return false;
  }

  char c = (char)ch;
  raw_byte_count++;

  raw_dump[raw_dump_head] = (uint8_t)c;
  raw_dump_head = (uint8_t)((raw_dump_head + 1U) % XY_RAW_DUMP_LEN);
  if (raw_dump_count < XY_RAW_DUMP_LEN) {
    raw_dump_count++;
  }

  if ((c == '\n') || (c == '\r')) {
    if (rx_line_idx > 0) {
      rx_line_buffer[rx_line_idx] = '\0';
      xy_msg_t parsed_msg;
      memset(&parsed_msg, 0, sizeof(parsed_msg));
      bool ok_xy = xy_link_parse_line(rx_line_buffer, &parsed_msg);
      if (ok_xy) {
        chMtxLock(&link_mutex);
        current_msg = parsed_msg;
        frame_count++;
        chMtxUnlock(&link_mutex);
        rx_line_idx = 0;
        return true;
      }
      gesture_event_t g = GESTURE_NONE;
      bool ok_gs = xy_link_parse_gesture_line(rx_line_buffer, &g);
      if (ok_gs && (gesture_mailbox_ptr != NULL)) {
        /* Non-blocking post (2026-09-05): this function runs on the SAME
         * thread that must keep draining SD3 for $XY pointer frames
         * (PointerLinkThread, see main.c). Blocking here up to 100ms
         * whenever the gesture mailbox is full - which a burst of real
         * gestures from the sensor can trigger now that it classifies
         * swipes/enter/exit itself - starves that read loop long enough
         * for the UART RX queue to overflow, freezing the on-screen
         * pointer. A gesture that finds no room is simply dropped
         * (best-effort - the mailbox already has 8 slots of headroom, and
         * losing an occasional queued gesture beats stalling the pointer
         * stream). */
        chMBPostTimeout(gesture_mailbox_ptr, (msg_t)g, TIME_IMMEDIATE);
      }
      rx_line_idx = 0;
    }
    return false;
  }

  if (rx_line_idx < (XY_LINE_BUF_SIZE - 1U)) {
    rx_line_buffer[rx_line_idx++] = c;
  } else {
    /* Line buffer overflow -> reset to prevent corruption */
    rx_line_idx = 0;
  }

  return false;
}

void xy_link_get_last_msg(xy_msg_t *out_msg) {
  if (out_msg != NULL) {
    chMtxLock(&link_mutex);
    *out_msg = current_msg;
    chMtxUnlock(&link_mutex);
  }
}

uint32_t xy_link_get_frame_count(void) {
  chMtxLock(&link_mutex);
  uint32_t count = frame_count;
  chMtxUnlock(&link_mutex);
  return count;
}

uint32_t xy_link_get_raw_byte_count(void) {
  return raw_byte_count;
}

uint8_t xy_link_get_raw_dump(uint8_t *out, uint8_t max_len) {
  uint8_t n = (raw_dump_count < max_len) ? raw_dump_count : max_len;
  uint8_t start = (uint8_t)((raw_dump_head + XY_RAW_DUMP_LEN - raw_dump_count) % XY_RAW_DUMP_LEN);
  uint8_t i;

  for (i = 0; i < n; i++) {
    out[i] = raw_dump[(start + i) % XY_RAW_DUMP_LEN];
  }
  return n;
}
