#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "xy_link.h"

/* Definition of mock SD3 for linking */
SerialDriver SD3;

static int failures = 0;

#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    failures++; \
  } \
} while (0)

static void test_valid_swipes(void) {
  gesture_event_t g = GESTURE_NONE;

  /* SWIPE_LEFT = 1 */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,1,1\r\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_LEFT);

  /* SWIPE_RIGHT = 2 */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,42,2\r\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_RIGHT);

  /* SWIPE_UP = 3 */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,999,3\r\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_UP);

  /* SWIPE_DOWN = 4 */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,100000,4\r\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_DOWN);

  /* Line ending variations (\n alone, \r alone) */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,10,1\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_LEFT);

  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,11,2\r", &g) == true);
  CHECK(g == GESTURE_SWIPE_RIGHT);
}

static void test_enter_id_5(void) {
  gesture_event_t g = GESTURE_NONE;

  /* ID 5 = ENTER on cursorSensor (<90mm) mapped to GESTURE_PUSH */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,50,5\r\n", &g) == true);
  CHECK(g == GESTURE_PUSH);

  /* Trailing variations */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,51,5\n", &g) == true);
  CHECK(g == GESTURE_PUSH);
}

static void test_excluded_id_6_and_invalid(void) {
  gesture_event_t g = GESTURE_NONE;

  /* ID 6 = EXIT on cursorSensor (>190mm). MUST NOT be mapped to HOLD or anything else! */
  g = (gesture_event_t)99;
  CHECK(xy_link_parse_gesture_line("$GS,52,6\r\n", &g) == false);
  CHECK(g == GESTURE_NONE);

  /* ID 0 = NONE */
  g = (gesture_event_t)99;
  CHECK(xy_link_parse_gesture_line("$GS,53,0\r\n", &g) == false);
  CHECK(g == GESTURE_NONE);

  /* Out of range IDs (8 and above - 7 is now valid, see test_valid_swipes) */
  g = (gesture_event_t)99;
  CHECK(xy_link_parse_gesture_line("$GS,54,8\r\n", &g) == false);
  CHECK(g == GESTURE_NONE);

  CHECK(xy_link_parse_gesture_line("$GS,55,255\r\n", &g) == false);
  CHECK(g == GESTURE_NONE);
}

static void test_presence_id_7(void) {
  gesture_event_t g = GESTURE_NONE;

  /* ID 7 = PRESENCE (rilevamento a lungo raggio, ~50cm) -> GESTURE_PRESENCE */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,60,7\r\n", &g) == true);
  CHECK(g == GESTURE_PRESENCE);
}

static void test_malformed_and_edge_cases(void) {
  gesture_event_t g = GESTURE_NONE;

  /* Null arguments */
  CHECK(xy_link_parse_gesture_line(NULL, &g) == false);
  CHECK(xy_link_parse_gesture_line("$GS,1,1\r\n", NULL) == false);

  /* Wrong prefix */
  CHECK(xy_link_parse_gesture_line("$XY,1,1,100,100\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("GS,1,1\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("$G,1,1\r\n", &g) == false);

  /* Missing fields */
  CHECK(xy_link_parse_gesture_line("$GS,\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("$GS,1\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("$GS,1,\r\n", &g) == false);

  /* Non-numeric */
  CHECK(xy_link_parse_gesture_line("$GS,abc,1\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("$GS,1,xyz\r\n", &g) == false);

  /* Trailing junk */
  CHECK(xy_link_parse_gesture_line("$GS,1,1extra\r\n", &g) == false);
  CHECK(xy_link_parse_gesture_line("$GS,1,1,2\r\n", &g) == false);
}

static void test_xy_and_gs_parser_separation(void) {
  xy_msg_t xy;
  gesture_event_t g;

  /* $XY parser accepts $XY but rejects $GS */
  memset(&xy, 0, sizeof(xy));
  CHECK(xy_link_parse_line("$XY,100,1,150,200\r\n", &xy) == true);
  CHECK(xy.seq == 100);
  CHECK(xy.valid == 1);
  CHECK(xy.x == 150);
  CHECK(xy.y == 200);

  memset(&xy, 0, sizeof(xy));
  CHECK(xy_link_parse_line("$GS,101,1\r\n", &xy) == false);

  /* $GS parser accepts $GS but rejects $XY */
  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$GS,101,1\r\n", &g) == true);
  CHECK(g == GESTURE_SWIPE_LEFT);

  g = GESTURE_NONE;
  CHECK(xy_link_parse_gesture_line("$XY,100,1,150,200\r\n", &g) == false);
}

int main(void) {
  printf("Testing xy_link_parse_gesture_line valid swipe events (1..4)...\n");
  test_valid_swipes();

  printf("Testing ID 5 (ENTER -> GESTURE_PUSH)...\n");
  test_enter_id_5();

  printf("Testing exclusion of ID 6 (EXIT) and invalid IDs...\n");
  test_excluded_id_6_and_invalid();

  printf("Testing ID 7 (PRESENCE -> GESTURE_PRESENCE)...\n");
  test_presence_id_7();

  printf("Testing malformed and edge cases...\n");
  test_malformed_and_edge_cases();

  printf("Testing $XY and $GS parser separation...\n");
  test_xy_and_gs_parser_separation();

  if (failures == 0) {
    printf("ALL TESTS PASSED\n");
    return 0;
  } else {
    printf("%d TESTS FAILED\n", failures);
    return 1;
  }
}
