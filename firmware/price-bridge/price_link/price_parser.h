#ifndef PRICE_PARSER_H
#define PRICE_PARSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PRICE_COUNT 4U
#define PRICE_MAX_CENTS 999999UL
#define PRICE_LINE_SIZE 96U
typedef struct {
  char line[PRICE_LINE_SIZE];
  size_t length;
  bool overflow;
  uint8_t step;
  uint32_t last_ms;
  uint32_t pending[PRICE_COUNT];
} price_parser_t;
extern const char *const price_names[PRICE_COUNT];
void price_parser_init(price_parser_t *parser);
/* Returns a complete validated list only after all eight ordered records.
 * Caller owns parser; out is unchanged for incomplete/invalid input. */
bool price_parser_feed(price_parser_t *parser, char byte, uint32_t now_ms,
                       uint32_t out[PRICE_COUNT]);
static inline uint32_t price_total_cents(uint16_t grams, uint32_t cents_per_kg) {
  return (uint32_t)(((uint64_t)grams * cents_per_kg) / 1000U);
}
#endif
