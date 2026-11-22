#include "keypad_link.h"
#include "chprintf.h"

static BaseSequentialStream *chp = NULL;
static uint32_t seq = 0;
static uint32_t gesture_seq = 0;
static bool has_sent = false;
static systime_t last_sent_time = 0;

static bool last_valid = false;
static uint16_t last_x = 0;
static uint16_t last_y = 0;

void keypad_link_init(BaseSequentialStream *stream) {
  chp = (stream != NULL) ? stream : (BaseSequentialStream *)&SD3;
  seq = 0;
  gesture_seq = 0;
  has_sent = false;
  last_sent_time = 0;
  last_valid = false;
  last_x = 0;
  last_y = 0;
}

void keypad_link_send_if_dirty(bool valid, uint16_t x, uint16_t y, systime_t now) {
  if (chp == NULL) {
    return;
  }

  bool dirty = !has_sent ||
               (valid != last_valid) ||
               (valid && ((x != last_x) || (y != last_y)));

  bool heartbeat = has_sent && (chTimeDiffX(last_sent_time, now) >= TIME_MS2I(500U));

  if (dirty || heartbeat) {
    chprintf(chp, "$XY,%u,%u,%u,%u\r\n",
             (unsigned int)seq++,
             valid ? 1U : 0U,
             (unsigned int)(valid ? x : 0U),
             (unsigned int)(valid ? y : 0U));

    last_valid = valid;
    last_x = valid ? x : 0U;
    last_y = valid ? y : 0U;
    last_sent_time = now;
    has_sent = true;
  }
}

void keypad_link_send_gesture(gesture_event_t g, systime_t now) {
  (void)now;
  if (chp == NULL || g == GESTURE_NONE) {
    return;
  }

  chprintf(chp, "$GS,%u,%u\r\n",
           (unsigned int)gesture_seq++,
           (unsigned int)g);
}
