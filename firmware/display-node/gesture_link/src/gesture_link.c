#include "gesture_link.h"

void gestureLinkParserInit(gesture_link_parser_t *p) {
  p->state = GL_WAIT_START;
  p->payload_idx = 0;
  p->checksum = 0;
}

bool gestureLinkParserFeed(gesture_link_parser_t *p, uint8_t byte, gesture_event_t *out) {

  switch (p->state) {
  case GL_WAIT_START:
    if (byte == GESTURE_LINK_FRAME_START)
      p->state = GL_WAIT_TYPE;
    break;

  case GL_WAIT_TYPE:
    p->type = byte;
    p->state = GL_WAIT_LEN;
    break;

  case GL_WAIT_LEN:
    if (byte == 0 || byte > sizeof(p->payload)) {
      p->state = GL_WAIT_START;
      break;
    }
    p->len = byte;
    p->payload_idx = 0;
    p->checksum = 0;
    p->state = GL_WAIT_PAYLOAD;
    break;

  case GL_WAIT_PAYLOAD:
    p->payload[p->payload_idx++] = byte;
    p->checksum ^= byte;
    if (p->payload_idx == p->len)
      p->state = GL_WAIT_CHECKSUM;
    break;

  case GL_WAIT_CHECKSUM:
    p->state = GL_WAIT_START;
    if (byte != p->checksum)
      break;
    if (p->type != GESTURE_LINK_MSG_GESTURE || p->len != sizeof(msg_gesture_t))
      break;
    if (p->payload[0] > GESTURE_HOLD)
      break;
    *out = (gesture_event_t)p->payload[0];
    return true;
  }

  return false;
}

uint8_t gestureLinkEncode(gesture_event_t g, uint8_t *buf) {
  uint8_t gesto = (uint8_t)g;

  buf[0] = GESTURE_LINK_FRAME_START;
  buf[1] = GESTURE_LINK_MSG_GESTURE;
  buf[2] = sizeof(msg_gesture_t);
  buf[3] = gesto;
  buf[4] = gesto;   /* XOR of a single payload byte is itself */
  return 5;
}
