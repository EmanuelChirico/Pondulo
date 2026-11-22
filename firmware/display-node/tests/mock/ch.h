#ifndef MOCK_CH_H
#define MOCK_CH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int32_t msg_t;
#define TIME_IMMEDIATE ((sysinterval_t)0)
#define MSG_OK ((msg_t)0)
#define MSG_TIMEOUT ((msg_t)-1)

typedef uint32_t sysinterval_t;
#define TIME_MS2I(ms) ((sysinterval_t)(ms))
#define TIME_I2MS(interval) ((uint32_t)(interval))

typedef uint32_t systime_t;
extern systime_t mock_system_time;
static inline systime_t chVTGetSystemTimeX(void) { return mock_system_time; }
static inline systime_t chTimeDiffX(systime_t start, systime_t end) { return end - start; }

typedef struct { int dummy; } mutex_t;
static inline void chMtxObjectInit(mutex_t *m) { (void)m; }
static inline void chMtxLock(mutex_t *m) { (void)m; }
static inline void chMtxUnlock(mutex_t *m) { (void)m; }

typedef struct {
  msg_t *buffer;
  size_t cnt;
  size_t rdptr;
  size_t wrptr;
} mailbox_t;

static inline msg_t chMBPostTimeout(mailbox_t *mb, msg_t msg, sysinterval_t t) {
  (void)mb;
  (void)msg;
  (void)t;
  return MSG_OK;
}

typedef struct { int dummy; } event_listener_t;
typedef uint32_t eventflags_t;

static inline void chEvtRegister(void *src, event_listener_t *el, int f) {
  (void)src;
  (void)el;
  (void)f;
}

static inline eventflags_t chEvtGetAndClearFlags(event_listener_t *el) {
  (void)el;
  return 0;
}

#endif /* MOCK_CH_H */
