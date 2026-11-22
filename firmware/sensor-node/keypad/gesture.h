#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__has_include)
  #if __has_include("ch.h")
    #include "ch.h"
  #endif
#endif

#if !defined(__CHIBIOS_RT__)
  /* Opaque systime_t for host-side unit testing */
  typedef uint32_t systime_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gesture event identifiers.
 * Transmitted in $GS,<seq>,<gesture_id>\r\n protocol.
 * Edge-triggered: emitted exactly once per confirmed gesture.
 */
typedef enum {
  GESTURE_NONE = 0,
  GESTURE_SWIPE_LEFT = 1,
  GESTURE_SWIPE_RIGHT = 2,
  GESTURE_SWIPE_UP = 3,
  GESTURE_SWIPE_DOWN = 4,
  GESTURE_ENTER = 5,
  GESTURE_EXIT = 6
} gesture_event_t;

/*
 * Buffer dimension:
 * At 100 Hz (10 ms per tick), 80 samples correspond to 800 ms history,
 * comfortably covering GESTURE_MAX_SEGMENT_MS (700 ms) with margin.
 * Stored in static/global RAM to respect strict embedded stack budgets.
 */
#define GESTURE_RING_SIZE                 80U

/*
 * Velocity thresholds for swipe FSM (in screen pixels per second):
 * Derived proportionally from Python reference thresholds (150/72 mm/s)
 * scaled to screen width (320 px) vs sensor FoV at ~200 mm.
 */
#define GESTURE_ACTIVATE_SPEED_PXPS       200U
#define GESTURE_DEACTIVATE_SPEED_PXPS     100U

/*
 * Minimum net displacement along the dominant axis to qualify as a swipe.
 */
#define GESTURE_MIN_DISPLACEMENT_PX       40U

/*
 * Axis dominance ratio: 1.2x (same as Python reference).
 * Integer ratio: net_dominant * 5 >= net_other * 6.
 */
#define GESTURE_DOMINANCE_NUM             6U
#define GESTURE_DOMINANCE_DEN             5U

/*
 * Trajectory straightness ratio: 0.7.
 * Pure path efficiency check: abs(net) * 10 >= total_path * 7.
 * Rejects wandering hands, circles, or zig-zags.
 */
#define GESTURE_STRAIGHTNESS_NUM          7U
#define GESTURE_STRAIGHTNESS_DEN          10U

/*
 * Safety timeout: force-close motion segment if speed stays high > 700 ms.
 */
#define GESTURE_MAX_SEGMENT_MS            700U

/*
 * Post-gesture debounce cooldown in milliseconds.
 * 400 ms provides responsive gesture cadence without false multi-triggers.
 */
#define GESTURE_COOLDOWN_MS               400U

/*
 * Depth proximity thresholds in millimeters (ToF sensor Z-axis):
 * Interaction range is 30..250 mm.
 * ENTER: hand approaches closer than 90 mm.
 * EXIT: hand retreats beyond 190 mm.
 * Neutral deadband: 90..190 mm prevents chatter.
 */
#define GESTURE_ENTER_NEAR_MM             90U
#define GESTURE_EXIT_FAR_MM               190U

/*
 * Proximity hold duration before confirming ENTER/EXIT (in ms).
 */
#define GESTURE_DEPTH_HOLD_MS             150U

/*
 * FSM internal states.
 */
typedef enum {
  SWIPE_STATE_IDLE = 0,
  SWIPE_STATE_MOTION = 1
} swipe_state_t;

typedef enum {
  DEPTH_STATE_NEUTRAL = 0,
  DEPTH_STATE_NEAR = 1,
  DEPTH_STATE_FAR = 2
} depth_state_t;

/*
 * Persistent state structure for gesture detection.
 * MUST be allocated statically (never on thread stack).
 */
typedef struct {
  /* Circular ring buffer of past coordinates */
  uint16_t ring_x[GESTURE_RING_SIZE];
  uint16_t ring_y[GESTURE_RING_SIZE];
  uint8_t ring_head;
  uint8_t ring_count;

  /* Previous valid sample tracking */
  bool has_prev_valid;
  uint16_t prev_x;
  uint16_t prev_y;

  /* Swipe FSM */
  uint8_t swipe_state;
  uint8_t motion_start_ring_idx;
  systime_t motion_start_time;
  systime_t last_gesture_time;

  /* Proximity Enter/Exit FSM */
  uint8_t depth_state;
  uint8_t depth_candidate;
  systime_t depth_candidate_start_time;
} gesture_state_t;

/*
 * Initialize or reset gesture state structure.
 */
void gesture_reset(gesture_state_t *st);

/*
 * Update gesture detector. Called at 100 Hz from UiTickThread.
 *
 * Parameters:
 *   st       Pointer to static gesture_state_t.
 *   valid    True if target hand is currently detected in 30..250 mm range.
 *   x, y     Smoothed screen coordinates (display_x_q8 >> 8, display_y_q8 >> 8),
 *            screen range 0..319 x 0..239.
 *            Coordinate convention: X increases to the right, Y increases downwards.
 *            (net_x > 0 -> SWIPE_RIGHT, net_x < 0 -> SWIPE_LEFT,
 *             net_y > 0 -> SWIPE_DOWN,  net_y < 0 -> SWIPE_UP)
 *   dist_mm  Distance in mm of closest zone (valid when valid is true).
 *   now      Current system time timestamp.
 *
 * Returns:
 *   gesture_event_t: GESTURE_NONE if no gesture was confirmed on this tick,
 *   or an edge-triggered gesture event code (single emission per event).
 */
gesture_event_t gesture_update(gesture_state_t *st, bool valid,
                               uint16_t x, uint16_t y, uint16_t dist_mm,
                               systime_t now);

#ifdef __cplusplus
}
#endif

#endif /* GESTURE_H */
