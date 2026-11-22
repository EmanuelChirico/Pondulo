#include "cursor.h"
#include <stddef.h>

void cursor_zone_to_pixel(uint8_t grid_dim, uint8_t zone, uint16_t *out_x, uint16_t *out_y) {
  if ((out_x == NULL) || (out_y == NULL) || (grid_dim == 0U)) {
    return;
  }

  uint8_t row = zone / grid_dim;
  uint8_t col = zone % grid_dim;

  uint16_t tile_w = (uint16_t)(SCREEN_W / grid_dim);
  uint16_t tile_h = (uint16_t)(SCREEN_H / grid_dim);

  *out_x = (uint16_t)(col * tile_w + (tile_w / 2U));
  *out_y = (uint16_t)(row * tile_h + (tile_h / 2U));
}

void cursor_calculate_centroid(const cursor_target_zone_t *zones, uint8_t count,
                               int32_t *out_row_q16, int32_t *out_col_q16) {
  if ((zones == NULL) || (count == 0U) || (out_row_q16 == NULL) || (out_col_q16 == NULL)) {
    return;
  }

  int64_t sum_w = 0;
  int64_t sum_row_w = 0;
  int64_t sum_col_w = 0;

  for (uint8_t i = 0; i < count; i++) {
    uint32_t dist = zones[i].dist_mm;
    uint32_t clamped_dist = (dist < 50U) ? 50U : dist;
    int64_t w = 65536LL / (int64_t)clamped_dist;

    sum_w += w;
    sum_row_w += w * (int64_t)zones[i].row;
    sum_col_w += w * (int64_t)zones[i].col;
  }

  if (sum_w > 0) {
    *out_row_q16 = (int32_t)(((sum_row_w * 65536LL) + (sum_w / 2LL)) / sum_w);
    *out_col_q16 = (int32_t)(((sum_col_w * 65536LL) + (sum_w / 2LL)) / sum_w);
  }
}

void cursor_centroid_to_pixel(int32_t row_centroid_q16, int32_t col_centroid_q16,
                              uint8_t grid_dim, uint16_t *out_x, uint16_t *out_y) {
  if ((out_x == NULL) || (out_y == NULL) || (grid_dim == 0U)) {
    return;
  }

  uint16_t tile_w = (uint16_t)(SCREEN_W / grid_dim);
  uint16_t tile_h = (uint16_t)(SCREEN_H / grid_dim);

  /* Scale Q16 fractional column/row to pixel coordinates with round-to-nearest */
  int64_t px = (((int64_t)col_centroid_q16 * tile_w) + 32768LL) / 65536LL + (int64_t)(tile_w / 2U);
  int64_t py = (((int64_t)row_centroid_q16 * tile_h) + 32768LL) / 65536LL + (int64_t)(tile_h / 2U);

  /* Boundary clamping */
  if (px < 0) {
    px = 0;
  } else if (px >= (int64_t)SCREEN_W) {
    px = (int64_t)SCREEN_W - 1;
  }

  if (py < 0) {
    py = 0;
  } else if (py >= (int64_t)SCREEN_H) {
    py = (int64_t)SCREEN_H - 1;
  }

  *out_x = (uint16_t)px;
  *out_y = (uint16_t)py;
}

int32_t cursor_ema_update(int32_t current_q8, int32_t target_q8, int32_t alpha_num, int32_t alpha_den) {
  int32_t error = target_q8 - current_q8;
  int32_t step = (error * alpha_num) / alpha_den;
  if ((step == 0) && (error != 0)) {
    step = (error > 0) ? 1 : -1;  /* forza convergenza minima per evitare stallo fixed-point */
  }
  return current_q8 + step;
}

