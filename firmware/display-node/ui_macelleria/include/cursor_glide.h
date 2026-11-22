/*
 * cursor_glide.h - Pure C adaptive EMA filter (One Euro Filter fixed-point Q8)
 * for smooth cursor motion on display UI.
 *
 * Implements speed-dependent smoothing with zero-stall correction:
 * - Low speed: maximum damping (alpha=CURSOR_ALPHA_MIN) to suppress sensor noise.
 * - High speed: responsive tracking (alpha=CURSOR_ALPHA_MAX) to prevent lag.
 * - Derivative smoothing: suppresses alternating sign jitter.
 * - Snap on first detection / reset: no cross-screen drift.
 */

#ifndef CURSOR_GLIDE_H
#define CURSOR_GLIDE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CURSOR_SCREEN_W 320U
#define CURSOR_SCREEN_H 240U

/*
 * Adaptive EMA filter parameters:
 * - Minimum speed / jitter zone: alpha minimum CURSOR_ALPHA_MIN (8/100)
 * - Maximum speed / swipe zone: alpha maximum CURSOR_ALPHA_MAX (35/100)
 * - Derivative filter: CURSOR_SPEED_ALPHA (5/100)
 */
#define CURSOR_ALPHA_DEN         100
#define CURSOR_ALPHA_MIN         8
#define CURSOR_ALPHA_MAX         35

#define CURSOR_SPEED_ALPHA_NUM   5
#define CURSOR_SPEED_ALPHA_DEN   100

/*
 * Speed thresholds in Q8 pixel/tick:
 * 20 px/s  -> (20 * 256) / 100 = 51 Q8
 * 250 px/s -> (250 * 256) / 100 = 640 Q8
 */
#define CURSOR_SPEED_MIN_Q8      ((20 * 256) / 100)
#define CURSOR_SPEED_MAX_Q8      ((250 * 256) / 100)

/*
 * Pure, deterministic, symmetric velocity -> alpha mapping function:
 * f(v) == f(-v).
 * Pure: no internal state, no globals, no side-effects.
 */
int32_t cursor_speed_to_alpha(int32_t speed_q8);

/*
 * One-step EMA update in Q8 fixed-point with zero-stall correction:
 * if error != 0 but integer division yields step == 0, forces step = +/-1
 * to guarantee complete convergence.
 */
int32_t cursor_ema_update(int32_t current_q8, int32_t target_q8, int32_t alpha_num, int32_t alpha_den);

/*
 * Single-axis adaptive filter state.
 */
typedef struct {
  int32_t current_q8;
  int32_t prev_target_q8;
  int32_t filtered_speed_q8;
  bool initialized;
} cursor_axis_filter_t;

/*
 * 2D glide filter state.
 */
typedef struct {
  cursor_axis_filter_t x;
  cursor_axis_filter_t y;
} cursor_glide_t;

void cursor_glide_init(cursor_glide_t *glide);
void cursor_glide_reset(cursor_glide_t *glide);
void cursor_glide_update(cursor_glide_t *glide, uint16_t raw_x, uint16_t raw_y,
                         uint16_t *filtered_x, uint16_t *filtered_y);

#ifdef __cplusplus
}
#endif

#endif /* CURSOR_GLIDE_H */
