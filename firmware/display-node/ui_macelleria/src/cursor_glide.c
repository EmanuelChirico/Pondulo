/*
 * cursor_glide.c - Pure C adaptive EMA filter (One Euro Filter fixed-point Q8).
 *
 * Independent implementation for STM32 uGFX display UI.
 * Zero external dependencies beyond standard C headers.
 */

#include "cursor_glide.h"
#include <stddef.h>

int32_t cursor_speed_to_alpha(int32_t speed_q8) {
  int32_t s = (speed_q8 >= 0) ? speed_q8 : -speed_q8;
  if (s <= CURSOR_SPEED_MIN_Q8) {
    return CURSOR_ALPHA_MIN;
  }
  if (s >= CURSOR_SPEED_MAX_Q8) {
    return CURSOR_ALPHA_MAX;
  }
  return CURSOR_ALPHA_MIN +
         ((CURSOR_ALPHA_MAX - CURSOR_ALPHA_MIN) * (s - CURSOR_SPEED_MIN_Q8)) /
         (CURSOR_SPEED_MAX_Q8 - CURSOR_SPEED_MIN_Q8);
}

int32_t cursor_ema_update(int32_t current_q8, int32_t target_q8, int32_t alpha_num, int32_t alpha_den) {
  int32_t error = target_q8 - current_q8;
  int32_t step = (error * alpha_num) / alpha_den;
  if ((step == 0) && (error != 0)) {
    step = (error > 0) ? 1 : -1;  /* Force minimal step to prevent fixed-point stall */
  }
  return current_q8 + step;
}

static void cursor_axis_reset(cursor_axis_filter_t *axis) {
  if (axis == NULL) {
    return;
  }
  axis->current_q8 = 0;
  axis->prev_target_q8 = 0;
  axis->filtered_speed_q8 = 0;
  axis->initialized = false;
}

static int32_t cursor_axis_update(cursor_axis_filter_t *axis, int32_t new_target_q8) {
  if (axis == NULL) {
    return new_target_q8;
  }

  if (!axis->initialized) {
    /* First sample after reset: instant snap without interpolation */
    axis->current_q8 = new_target_q8;
    axis->prev_target_q8 = new_target_q8;
    axis->filtered_speed_q8 = 0;
    axis->initialized = true;
    return new_target_q8;
  }

  int32_t raw_delta_q8 = new_target_q8 - axis->prev_target_q8;
  axis->prev_target_q8 = new_target_q8;

  /* Low-pass filter on signed derivative to cancel alternating high-frequency jitter */
  axis->filtered_speed_q8 = cursor_ema_update(axis->filtered_speed_q8, raw_delta_q8,
                                              CURSOR_SPEED_ALPHA_NUM, CURSOR_SPEED_ALPHA_DEN);

  /* Pure and deterministic speed -> alpha function */
  int32_t alpha = cursor_speed_to_alpha(axis->filtered_speed_q8);

  /* Position filtering with zero-stall convergence */
  axis->current_q8 = cursor_ema_update(axis->current_q8, new_target_q8, alpha, CURSOR_ALPHA_DEN);
  return axis->current_q8;
}

void cursor_glide_init(cursor_glide_t *glide) {
  cursor_glide_reset(glide);
}

void cursor_glide_reset(cursor_glide_t *glide) {
  if (glide == NULL) {
    return;
  }
  cursor_axis_reset(&glide->x);
  cursor_axis_reset(&glide->y);
}

void cursor_glide_update(cursor_glide_t *glide, uint16_t raw_x, uint16_t raw_y,
                         uint16_t *filtered_x, uint16_t *filtered_y) {
  if (glide == NULL) {
    if (filtered_x != NULL) *filtered_x = raw_x;
    if (filtered_y != NULL) *filtered_y = raw_y;
    return;
  }

  int32_t target_x_q8 = (int32_t)raw_x << 8;
  int32_t target_y_q8 = (int32_t)raw_y << 8;

  int32_t fx_q8 = cursor_axis_update(&glide->x, target_x_q8);
  int32_t fy_q8 = cursor_axis_update(&glide->y, target_y_q8);

  /* Round to nearest integer pixel */
  int32_t px = (fx_q8 + 128) >> 8;
  int32_t py = (fy_q8 + 128) >> 8;

  /* Clamp to screen bounds */
  if (px < 0) {
    px = 0;
  } else if (px >= (int32_t)CURSOR_SCREEN_W) {
    px = (int32_t)CURSOR_SCREEN_W - 1;
  }

  if (py < 0) {
    py = 0;
  } else if (py >= (int32_t)CURSOR_SCREEN_H) {
    py = (int32_t)CURSOR_SCREEN_H - 1;
  }

  if (filtered_x != NULL) {
    *filtered_x = (uint16_t)px;
  }
  if (filtered_y != NULL) {
    *filtered_y = (uint16_t)py;
  }
}
