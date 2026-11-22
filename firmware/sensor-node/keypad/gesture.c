#include "gesture.h"

void gesture_reset(gesture_state_t *st) {
  if (st == NULL) {
    return;
  }
  st->ring_head = 0;
  st->ring_count = 0;
  st->has_prev_valid = false;
  st->prev_x = 0;
  st->prev_y = 0;
  st->swipe_state = (uint8_t)SWIPE_STATE_IDLE;
  st->motion_start_ring_idx = 0;
  st->motion_start_time = 0;
  st->last_gesture_time = 0;
  st->depth_state = (uint8_t)DEPTH_STATE_NEUTRAL;
  st->depth_candidate = (uint8_t)DEPTH_STATE_NEUTRAL;
  st->depth_candidate_start_time = 0;
}

static gesture_event_t evaluate_swipe_segment(gesture_state_t *st,
                                              uint8_t start_idx,
                                              uint8_t end_idx,
                                              systime_t now) {
  if (start_idx == end_idx) {
    return GESTURE_NONE;
  }

  /* Calculate net displacement along X and Y */
  int32_t net_x = (int32_t)st->ring_x[end_idx] - (int32_t)st->ring_x[start_idx];
  int32_t net_y = (int32_t)st->ring_y[end_idx] - (int32_t)st->ring_y[start_idx];

  int32_t abs_net_x = (net_x >= 0) ? net_x : -net_x;
  int32_t abs_net_y = (net_y >= 0) ? net_y : -net_y;

  /* Traverse trajectory from start_idx to end_idx calculating total path length */
  uint32_t path_x = 0;
  uint32_t path_y = 0;
  uint8_t curr = start_idx;
  uint8_t steps = 0;

  while (curr != end_idx && steps < GESTURE_RING_SIZE) {
    uint8_t next = curr + 1U;
    if (next >= GESTURE_RING_SIZE) {
      next = 0;
    }
    int32_t step_x = (int32_t)st->ring_x[next] - (int32_t)st->ring_x[curr];
    int32_t step_y = (int32_t)st->ring_y[next] - (int32_t)st->ring_y[curr];
    path_x += (uint32_t)((step_x >= 0) ? step_x : -step_x);
    path_y += (uint32_t)((step_y >= 0) ? step_y : -step_y);
    curr = next;
    steps++;
  }

  /* Cooldown check (debounce post-gesture) */
  if (st->last_gesture_time != 0) {
    systime_t dt = now - st->last_gesture_time;
    if (dt < GESTURE_COOLDOWN_MS) {
      return GESTURE_NONE;
    }
  }

  /* Check dominant axis and classify */
  if (abs_net_x >= abs_net_y) {
    /* Horizontal dominant */
    if ((uint32_t)abs_net_x < GESTURE_MIN_DISPLACEMENT_PX) {
      return GESTURE_NONE;
    }
    /* Dominance check: net_x * 5 >= net_y * 6 (1.2x) */
    if ((uint64_t)abs_net_x * GESTURE_DOMINANCE_DEN < (uint64_t)abs_net_y * GESTURE_DOMINANCE_NUM) {
      return GESTURE_NONE;
    }
    /* Straightness check: net_x * 10 >= path_x * 7 (0.7) */
    if ((uint64_t)abs_net_x * GESTURE_STRAIGHTNESS_DEN < (uint64_t)path_x * GESTURE_STRAIGHTNESS_NUM) {
      return GESTURE_NONE;
    }

    st->last_gesture_time = now;
    return (net_x > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
  } else {
    /* Vertical dominant */
    if ((uint32_t)abs_net_y < GESTURE_MIN_DISPLACEMENT_PX) {
      return GESTURE_NONE;
    }
    /* Dominance check: net_y * 5 >= net_x * 6 (1.2x) */
    if ((uint64_t)abs_net_y * GESTURE_DOMINANCE_DEN < (uint64_t)abs_net_x * GESTURE_DOMINANCE_NUM) {
      return GESTURE_NONE;
    }
    /* Straightness check: net_y * 10 >= path_y * 7 (0.7) */
    if ((uint64_t)abs_net_y * GESTURE_STRAIGHTNESS_DEN < (uint64_t)path_y * GESTURE_STRAIGHTNESS_NUM) {
      return GESTURE_NONE;
    }

    st->last_gesture_time = now;
    return (net_y > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
  }
}

gesture_event_t gesture_update(gesture_state_t *st, bool valid,
                               uint16_t x, uint16_t y, uint16_t dist_mm,
                               systime_t now) {
  if (st == NULL) {
    return GESTURE_NONE;
  }

  gesture_event_t swipe_event = GESTURE_NONE;

  if (valid) {
    /* 1. Store coordinate into ring buffer */
    uint8_t curr_ring_idx = st->ring_head;
    st->ring_x[curr_ring_idx] = x;
    st->ring_y[curr_ring_idx] = y;

    st->ring_head++;
    if (st->ring_head >= GESTURE_RING_SIZE) {
      st->ring_head = 0;
    }
    if (st->ring_count < GESTURE_RING_SIZE) {
      st->ring_count++;
    }

    /* 2. Compute instantaneous velocity */
    uint32_t speed_px_s = 0;
    if (st->has_prev_valid) {
      int32_t dx = (int32_t)x - (int32_t)st->prev_x;
      int32_t dy = (int32_t)y - (int32_t)st->prev_y;
      int32_t abs_dx = (dx >= 0) ? dx : -dx;
      int32_t abs_dy = (dy >= 0) ? dy : -dy;

      /* Integer approximation of sqrt(dx^2 + dy^2): max + min/2 */
      int32_t max_d = (abs_dx > abs_dy) ? abs_dx : abs_dy;
      int32_t min_d = (abs_dx > abs_dy) ? abs_dy : abs_dx;
      uint32_t delta_px = (uint32_t)(max_d + (min_d >> 1));
      speed_px_s = delta_px * 100U; /* 100 Hz fixed tick -> dt = 10 ms = 0.01 s */
    }

    /* 3. Swipe FSM state transitions */
    if (st->swipe_state == (uint8_t)SWIPE_STATE_IDLE) {
      if (st->has_prev_valid && (speed_px_s >= GESTURE_ACTIVATE_SPEED_PXPS)) {
        st->swipe_state = (uint8_t)SWIPE_STATE_MOTION;
        /* Motion starts from previous sample index */
        st->motion_start_ring_idx = (curr_ring_idx > 0) ? (curr_ring_idx - 1U) : (GESTURE_RING_SIZE - 1U);
        st->motion_start_time = now;
      }
    } else {
      /* In SWIPE_STATE_MOTION */
      bool timeout = ((now - st->motion_start_time) > GESTURE_MAX_SEGMENT_MS);
      bool slowed_down = (speed_px_s < GESTURE_DEACTIVATE_SPEED_PXPS);

      if (slowed_down || timeout) {
        swipe_event = evaluate_swipe_segment(st, st->motion_start_ring_idx, curr_ring_idx, now);
        st->swipe_state = (uint8_t)SWIPE_STATE_IDLE;
      }
    }

    st->has_prev_valid = true;
    st->prev_x = x;
    st->prev_y = y;
  } else {
    /* Target lost */
    if (st->swipe_state == (uint8_t)SWIPE_STATE_MOTION) {
      uint8_t last_valid_idx = (st->ring_head > 0) ? (st->ring_head - 1U) : (GESTURE_RING_SIZE - 1U);
      swipe_event = evaluate_swipe_segment(st, st->motion_start_ring_idx, last_valid_idx, now);
      st->swipe_state = (uint8_t)SWIPE_STATE_IDLE;
    }
    st->has_prev_valid = false;
  }

  /* 4. Depth Proximity FSM (ENTER / EXIT) */
  gesture_event_t depth_event = GESTURE_NONE;

  if (valid && dist_mm > 0) {
    uint8_t candidate = (uint8_t)DEPTH_STATE_NEUTRAL;
    if (dist_mm < GESTURE_ENTER_NEAR_MM) {
      candidate = (uint8_t)DEPTH_STATE_NEAR;
    } else if (dist_mm > GESTURE_EXIT_FAR_MM) {
      candidate = (uint8_t)DEPTH_STATE_FAR;
    } else {
      candidate = (uint8_t)DEPTH_STATE_NEUTRAL;
    }

    if (candidate != st->depth_state) {
      if (candidate != st->depth_candidate) {
        st->depth_candidate = candidate;
        st->depth_candidate_start_time = now;
      } else {
        if ((now - st->depth_candidate_start_time) >= GESTURE_DEPTH_HOLD_MS) {
          uint8_t old_state = st->depth_state;
          st->depth_state = candidate;

          if (candidate == (uint8_t)DEPTH_STATE_NEAR && old_state != (uint8_t)DEPTH_STATE_NEAR) {
            depth_event = GESTURE_ENTER;
          } else if (candidate == (uint8_t)DEPTH_STATE_FAR && old_state != (uint8_t)DEPTH_STATE_FAR) {
            depth_event = GESTURE_EXIT;
          }
        }
      }
    } else {
      st->depth_candidate = candidate;
      st->depth_candidate_start_time = now;
    }
  } else {
    /* If hand is withdrawn (valid=false) while in NEAR state, emit GESTURE_EXIT immediately */
    if (st->depth_state == (uint8_t)DEPTH_STATE_NEAR) {
      depth_event = GESTURE_EXIT;
    }
    /* Reset depth state to NEUTRAL when hand is absent */
    st->depth_state = (uint8_t)DEPTH_STATE_NEUTRAL;
    st->depth_candidate = (uint8_t)DEPTH_STATE_NEUTRAL;
    st->depth_candidate_start_time = now;
  }

  /* Proximity events have precedence as they represent deliberate Z-axis action */
  if (depth_event != GESTURE_NONE) {
    return depth_event;
  }
  return swipe_event;
}
