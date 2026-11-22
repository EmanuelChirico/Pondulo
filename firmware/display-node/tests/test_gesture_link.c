/*
 * Host-side unit test for gesture_link (gesture_link/). No ChibiOS/HAL
 * dependency - builds and runs directly on the PC with a native gcc:
 *
 *   gcc -I../gesture_link/include -o test_gesture_link.exe \
 *       test_gesture_link.c ../gesture_link/src/gesture_link.c
 *   ./test_gesture_link.exe
 *
 * Exercises the protocol logic in isolation, independent of whether real
 * serial data is available from the ToF node board.
 */

#include "gesture_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

/* Feeds an entire byte buffer through the parser, one byte at a time (as
 * the real UART RX thread would). Returns the number of complete frames
 * decoded, and writes each decoded gesture into out[] (caller-sized). */
static int feed_all(gesture_link_parser_t *p, const uint8_t *buf, size_t len, gesture_event_t *out, int out_cap) {
  int n = 0;
  size_t i;

  for (i = 0; i < len; i++) {
    gesture_event_t g;
    if (gestureLinkParserFeed(p, buf[i], &g)) {
      if (n < out_cap)
        out[n] = g;
      n++;
    }
  }
  return n;
}

/* Every gesture_event_t value round-trips through encode -> parse. */
static void test_roundtrip_all_gestures(void) {
  gesture_event_t all[] = {
    GESTURE_NONE, GESTURE_SWIPE_LEFT, GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP, GESTURE_SWIPE_DOWN, GESTURE_PUSH, GESTURE_HOLD
  };
  size_t i;

  for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
    gesture_link_parser_t p;
    uint8_t frame[5];
    uint8_t n = gestureLinkEncode(all[i], frame);
    gesture_event_t decoded[1];
    int count;

    CHECK(n == 5);
    gestureLinkParserInit(&p);
    count = feed_all(&p, frame, n, decoded, 1);
    CHECK(count == 1);
    CHECK(decoded[0] == all[i]);
  }
}

/* Encoded frame layout matches the spec exactly: START, TYPE, LEN, payload,
 * checksum (XOR of payload bytes only). */
static void test_frame_layout(void) {
  uint8_t frame[5];
  uint8_t n = gestureLinkEncode(GESTURE_SWIPE_LEFT, frame);

  CHECK(n == 5);
  CHECK(frame[0] == GESTURE_LINK_FRAME_START);
  CHECK(frame[1] == GESTURE_LINK_MSG_GESTURE);
  CHECK(frame[2] == sizeof(msg_gesture_t));
  CHECK(frame[3] == (uint8_t)GESTURE_SWIPE_LEFT);
  CHECK(frame[4] == (uint8_t)GESTURE_SWIPE_LEFT); /* XOR of one byte is itself */
}

/* Two valid frames back-to-back both decode, with nothing lost or merged. */
static void test_back_to_back_frames(void) {
  gesture_link_parser_t p;
  uint8_t buf[10];
  uint8_t n1, n2;
  gesture_event_t decoded[2];
  int count;

  n1 = gestureLinkEncode(GESTURE_SWIPE_UP, buf);
  n2 = gestureLinkEncode(GESTURE_HOLD, buf + n1);

  gestureLinkParserInit(&p);
  count = feed_all(&p, buf, n1 + n2, decoded, 2);
  CHECK(count == 2);
  CHECK(decoded[0] == GESTURE_SWIPE_UP);
  CHECK(decoded[1] == GESTURE_HOLD);
}

/* Leading garbage bytes (line noise before the real link starts, or before
 * the first 0xAA if RX begins mid-frame) must not desync the parser
 * permanently - it should resync on the next valid START byte. */
static void test_resync_after_garbage(void) {
  gesture_link_parser_t p;
  uint8_t buf[16];
  size_t idx = 0;
  uint8_t n;
  gesture_event_t decoded[1];
  int count;

  buf[idx++] = 0x00;
  buf[idx++] = 0xFF;
  buf[idx++] = 0x12;
  n = gestureLinkEncode(GESTURE_PUSH, buf + idx);
  idx += n;

  gestureLinkParserInit(&p);
  count = feed_all(&p, buf, idx, decoded, 1);
  CHECK(count == 1);
  CHECK(decoded[0] == GESTURE_PUSH);
}

/* A frame with a corrupted checksum byte must be dropped silently (no
 * decode), and the parser must still be usable for the next valid frame. */
static void test_bad_checksum_dropped(void) {
  gesture_link_parser_t p;
  uint8_t bad[5];
  uint8_t good[5];
  uint8_t buf[10];
  uint8_t n_bad, n_good;
  gesture_event_t decoded[2];
  int count;

  n_bad = gestureLinkEncode(GESTURE_SWIPE_DOWN, bad);
  bad[4] ^= 0xFF;   /* corrupt the checksum byte */

  n_good = gestureLinkEncode(GESTURE_SWIPE_RIGHT, good);

  memcpy(buf, bad, n_bad);
  memcpy(buf + n_bad, good, n_good);

  gestureLinkParserInit(&p);
  count = feed_all(&p, buf, n_bad + n_good, decoded, 2);
  CHECK(count == 1);
  CHECK(decoded[0] == GESTURE_SWIPE_RIGHT);
}

/* An unrecognized TIPO byte must not be decoded as a gesture, even if LEN
 * and CHECKSUM would otherwise be self-consistent. */
static void test_unknown_type_rejected(void) {
  gesture_link_parser_t p;
  uint8_t frame[5];
  gesture_event_t decoded[1];
  int count;

  frame[0] = GESTURE_LINK_FRAME_START;
  frame[1] = 0x02;              /* not MSG_TYPE_GESTURE */
  frame[2] = 1;
  frame[3] = (uint8_t)GESTURE_PUSH;
  frame[4] = (uint8_t)GESTURE_PUSH;   /* checksum still "valid" */

  gestureLinkParserInit(&p);
  count = feed_all(&p, frame, sizeof(frame), decoded, 1);
  CHECK(count == 0);
}

/* An out-of-range gesture byte (beyond GESTURE_HOLD) is rejected even with
 * a structurally valid frame - guards against a future protocol widening
 * on the sender side silently producing garbage on this side. */
static void test_out_of_range_gesture_rejected(void) {
  gesture_link_parser_t p;
  uint8_t frame[5];
  gesture_event_t decoded[1];
  int count;

  frame[0] = GESTURE_LINK_FRAME_START;
  frame[1] = GESTURE_LINK_MSG_GESTURE;
  frame[2] = 1;
  frame[3] = 0x7F;   /* not a valid gesture_event_t value */
  frame[4] = 0x7F;

  gestureLinkParserInit(&p);
  count = feed_all(&p, frame, sizeof(frame), decoded, 1);
  CHECK(count == 0);
}

/* LEN of 0 or larger than the parser's payload buffer must be rejected
 * (resync to WAIT_START) rather than desyncing or overflowing. */
static void test_bad_length_rejected(void) {
  gesture_link_parser_t p;
  uint8_t frame[5];
  gesture_event_t decoded[1];
  int count;

  frame[0] = GESTURE_LINK_FRAME_START;
  frame[1] = GESTURE_LINK_MSG_GESTURE;
  frame[2] = 0;   /* invalid: LEN == 0 */
  gestureLinkParserInit(&p);
  count = feed_all(&p, frame, 3, decoded, 1);
  CHECK(count == 0);

  /* parser must have resynced: a valid frame right after still decodes */
  {
    uint8_t good[5];
    uint8_t n = gestureLinkEncode(GESTURE_SWIPE_LEFT, good);
    count = feed_all(&p, good, n, decoded, 1);
    CHECK(count == 1);
    CHECK(decoded[0] == GESTURE_SWIPE_LEFT);
  }
}

int main(void) {
  test_roundtrip_all_gestures();
  test_frame_layout();
  test_back_to_back_frames();
  test_resync_after_garbage();
  test_bad_checksum_dropped();
  test_unknown_type_rejected();
  test_out_of_range_gesture_rejected();
  test_bad_length_rejected();

  if (failures == 0) {
    printf("All tests passed.\n");
    return 0;
  }
  printf("%d check(s) failed.\n", failures);
  return 1;
}
