/*
 * Host-side unit test for cursor_glide (ui_macelleria/src/cursor_glide.c).
 * Pure C - builds and runs directly on the PC with host gcc:
 *
 *   gcc -Wall -Wextra -I../ui_macelleria/include -o test_cursor_glide \
 *       test_cursor_glide.c ../ui_macelleria/src/cursor_glide.c
 *   ./test_cursor_glide
 */

#include "cursor_glide.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

static void test_speed_to_alpha_purity_symmetry_determinism(void) {
  printf("Testing speed_to_alpha (purity, determinism, symmetry f(v) == f(-v))...\n");

  /* 1. Bounds check and edge conditions */
  CHECK(cursor_speed_to_alpha(0) == CURSOR_ALPHA_MIN);
  CHECK(cursor_speed_to_alpha(CURSOR_SPEED_MIN_Q8) == CURSOR_ALPHA_MIN);
  CHECK(cursor_speed_to_alpha(-CURSOR_SPEED_MIN_Q8) == CURSOR_ALPHA_MIN);
  CHECK(cursor_speed_to_alpha(CURSOR_SPEED_MAX_Q8) == CURSOR_ALPHA_MAX);
  CHECK(cursor_speed_to_alpha(-CURSOR_SPEED_MAX_Q8) == CURSOR_ALPHA_MAX);
  CHECK(cursor_speed_to_alpha(CURSOR_SPEED_MAX_Q8 + 1000) == CURSOR_ALPHA_MAX);
  CHECK(cursor_speed_to_alpha(-(CURSOR_SPEED_MAX_Q8 + 1000)) == CURSOR_ALPHA_MAX);

  /* 2. Strict symmetry f(v) == f(-v) across broad range */
  for (int32_t v = -5000; v <= 5000; v++) {
    int32_t a_pos = cursor_speed_to_alpha(v >= 0 ? v : -v);
    int32_t a_neg = cursor_speed_to_alpha(v >= 0 ? -v : v);
    CHECK(a_pos == a_neg);
    CHECK(a_pos >= CURSOR_ALPHA_MIN);
    CHECK(a_pos <= CURSOR_ALPHA_MAX);
  }

  /* 3. Determinism: repeated calls with same input produce identical outputs */
  for (int i = 0; i < 100; i++) {
    CHECK(cursor_speed_to_alpha(100) == cursor_speed_to_alpha(100));
    CHECK(cursor_speed_to_alpha(-250) == cursor_speed_to_alpha(-250));
  }

  /* 4. Monotonicity for non-negative v */
  int32_t prev_alpha = CURSOR_ALPHA_MIN;
  for (int32_t v = 0; v <= CURSOR_SPEED_MAX_Q8 + 100; v += 5) {
    int32_t alpha = cursor_speed_to_alpha(v);
    CHECK(alpha >= prev_alpha);
    prev_alpha = alpha;
  }
}

static void test_ema_zero_stall_convergence(void) {
  printf("Testing cursor_ema_update zero-stall convergence...\n");

  /* Forward convergence with minimal error */
  int32_t current = 0;
  int32_t target = 10;
  for (int step = 0; step < 50 && current != target; step++) {
    current = cursor_ema_update(current, target, CURSOR_ALPHA_MIN, CURSOR_ALPHA_DEN);
  }
  CHECK(current == target);

  /* Backward convergence with minimal error */
  current = 10;
  target = 0;
  for (int step = 0; step < 50 && current != target; step++) {
    current = cursor_ema_update(current, target, CURSOR_ALPHA_MIN, CURSOR_ALPHA_DEN);
  }
  CHECK(current == target);

  /* Single unit error (step would divide to 0 without zero-stall fix) */
  current = 100;
  target = 101;
  int32_t updated = cursor_ema_update(current, target, CURSOR_ALPHA_MIN, CURSOR_ALPHA_DEN);
  CHECK(updated == 101);

  current = 100;
  target = 99;
  updated = cursor_ema_update(current, target, CURSOR_ALPHA_MIN, CURSOR_ALPHA_DEN);
  CHECK(updated == 99);
}

static void test_glide_snap_and_filtering(void) {
  printf("Testing cursor_glide snap, bounds clamping, and motion smoothing...\n");

  cursor_glide_t glide;
  cursor_glide_init(&glide);

  uint16_t fx = 0, fy = 0;

  /* 1. Snap on first sample */
  cursor_glide_update(&glide, 150, 100, &fx, &fy);
  CHECK(fx == 150);
  CHECK(fy == 100);

  /* 2. Continuous movement: filtered coordinates move towards target smoothly */
  cursor_glide_update(&glide, 200, 150, &fx, &fy);
  CHECK(fx > 150 && fx < 200);
  CHECK(fy > 100 && fy < 150);

  /* 3. Full convergence if target stays constant */
  for (int i = 0; i < 100; i++) {
    cursor_glide_update(&glide, 200, 150, &fx, &fy);
  }
  CHECK(fx == 200);
  CHECK(fy == 150);

  /* 4. Reset causes instant snap to new position (no gliding across screen) */
  cursor_glide_reset(&glide);
  cursor_glide_update(&glide, 10, 20, &fx, &fy);
  CHECK(fx == 10);
  CHECK(fy == 20);

  /* 5. Clamping to screen boundaries */
  cursor_glide_reset(&glide);
  cursor_glide_update(&glide, 500, 400, &fx, &fy);
  CHECK(fx == CURSOR_SCREEN_W - 1);
  CHECK(fy == CURSOR_SCREEN_H - 1);
}

int main(void) {
  test_speed_to_alpha_purity_symmetry_determinism();
  test_ema_zero_stall_convergence();
  test_glide_snap_and_filtering();

  if (failures == 0) {
    printf("ALL TESTS PASSED\n");
    return 0;
  } else {
    printf("%d TEST(S) FAILED\n", failures);
    return 1;
  }
}
