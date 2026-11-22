#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

#define SCREEN_W 320U
#define SCREEN_H 240U

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maps sensor zone to screen pixel coordinates (center of corresponding tile).
 * grid_dim is typically 8 (8x8 mode) or 4 (4x4 mode).
 * Computes integer linear mapping:
 *   row = zone / grid_dim;
 *   col = zone % grid_dim;
 *   *out_x = col * (SCREEN_W / grid_dim) + (SCREEN_W / grid_dim) / 2;
 *   *out_y = row * (SCREEN_H / grid_dim) + (SCREEN_H / grid_dim) / 2;
 * Zero float runtime.
 */
void cursor_zone_to_pixel(uint8_t grid_dim, uint8_t zone, uint16_t *out_x, uint16_t *out_y);

/*
 * Maximum distance deviation (in mm) between closest zone and its 4-neighbors
 * to be admitted into the weighted centroid cluster.
 */
#define NEIGHBOR_DIST_MARGIN_MM 40U

/*
 * EMA smoothing filter parameters:
 * At 100Hz tick rate, alpha = 35/100 (0.35).
 * Residual error drops to ~10% of initial value in ~5 ticks (50ms),
 * which is faster than the sensor frame period at 15Hz (66.6ms).
 * This ensures the cursor smoothly finishes gliding before the next sensor frame arrives.
 */
#define ALPHA_NUM 35
#define ALPHA_DEN 100

/*
 * Structure representing a zone measurement for centroid calculation.
 */
typedef struct {
  uint8_t row;
  uint8_t col;
  uint16_t dist_mm;
} cursor_target_zone_t;

/*
 * Calculates weighted spatial centroid in Q16 for up to 5 zones (center + 4-neighbors).
 * Weight w_i = 65536 / max(dist_i, 50).
 * Uses int64_t accumulators to prevent overflow.
 * If only 1 zone is provided, degenerates exactly to row * 65536 and col * 65536.
 */
void cursor_calculate_centroid(const cursor_target_zone_t *zones, uint8_t count,
                               int32_t *out_row_q16, int32_t *out_col_q16);

/*
 * Maps fractional Q16 centroid row/column to screen pixel coordinates.
 * Same linear scaling as cursor_zone_to_pixel, evaluated with sub-pixel Q16 precision.
 * Degenerates exactly to cursor_zone_to_pixel when row_centroid_q16 = row * 65536
 * and col_centroid_q16 = col * 65536.
 */
void cursor_centroid_to_pixel(int32_t row_centroid_q16, int32_t col_centroid_q16,
                              uint8_t grid_dim, uint16_t *out_x, uint16_t *out_y);

/*
 * One-step EMA update in Q8 fixed-point with zero-stall correction.
 * If step == 0 and error != 0, forces step = +/-1 to guarantee convergence.
 * Returns the updated display position in Q8.
 */
int32_t cursor_ema_update(int32_t current_q8, int32_t target_q8, int32_t alpha_num, int32_t alpha_den);

#ifdef __cplusplus
}
#endif

#endif /* CURSOR_H */
