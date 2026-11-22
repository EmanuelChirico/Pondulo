#include "price_parser.h"
#include <string.h>
const char *const price_names[PRICE_COUNT] = {"Manzo", "Pollo", "Maiale", "Agnello"};
static const char *const add_prefix[PRICE_COUNT] = {
  "PRODUCT_ADD:1:Manzo:", "PRODUCT_ADD:2:Pollo:",
  "PRODUCT_ADD:3:Maiale:", "PRODUCT_ADD:4:Agnello:"
};
static const char *const code_line[PRICE_COUNT] = {
  "PRODUCT_CODE:1:0001", "PRODUCT_CODE:2:0002",
  "PRODUCT_CODE:3:0003", "PRODUCT_CODE:4:0004"
};
void price_parser_init(price_parser_t *p) { memset(p, 0, sizeof(*p)); }
static bool parse_cents(const char *s, uint32_t *out) {
  uint32_t n = 0;
  size_t digits = 0;
  while (*s) {
    if (*s < '0' || *s > '9' || ++digits > 6U) return false;
    n = n * 10U + (uint32_t)(*s++ - '0');
  }
  if (digits == 0U || n == 0U || n > PRICE_MAX_CENTS) return false;
  *out = n;
  return true;
}
static bool process_line(price_parser_t *p, uint32_t out[PRICE_COUNT]) {
  /* A new first record restarts an interrupted transmission. */
  if (strncmp(p->line, add_prefix[0], strlen(add_prefix[0])) == 0) p->step = 0;
  uint8_t index = p->step / 2U;
  if ((p->step & 1U) == 0U) {
    size_t n = strlen(add_prefix[index]);
    if (strncmp(p->line, add_prefix[index], n) != 0 ||
        !parse_cents(p->line + n, &p->pending[index])) {
      p->step = 0;
      return false;
    }
  } else if (strcmp(p->line, code_line[index]) != 0) {
    p->step = 0;
    return false;
  }
  if (++p->step == PRICE_COUNT * 2U) {
    memcpy(out, p->pending, sizeof(p->pending));
    p->step = 0;
    return true;
  }
  return false;
}
bool price_parser_feed(price_parser_t *p, char byte, uint32_t now_ms,
                       uint32_t out[PRICE_COUNT]) {
  if ((p->length || p->step || p->overflow) &&
      (uint32_t)(now_ms - p->last_ms) > 2000U) price_parser_init(p);
  p->last_ms = now_ms;
  if (byte == '\r') return false;
  if (byte == '\n') {
    bool ready = false;
    if (p->overflow) p->step = 0;
    else if (p->length) {
      p->line[p->length] = '\0';
      ready = process_line(p, out);
    }
    p->length = 0;
    p->overflow = false;
    return ready;
  }
  /* NUL/control characters invalidate the entire line, not just its suffix. */
  if ((unsigned char)byte < 32U || (unsigned char)byte > 126U) p->overflow = true;
  if (!p->overflow) {
    if (p->length + 1U < sizeof(p->line)) p->line[p->length++] = byte;
    else p->overflow = true;
  }
  return false;
}
