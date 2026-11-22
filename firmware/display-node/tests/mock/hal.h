#ifndef MOCK_HAL_H
#define MOCK_HAL_H

#include "ch.h"

typedef struct { int dummy; } SerialDriver;
extern SerialDriver SD3;
typedef void *ioportid_t;

static inline void* chnGetEventSource(void *p) { return p; }
static inline msg_t sdGetTimeout(SerialDriver *sd, sysinterval_t t) {
  (void)sd;
  (void)t;
  return MSG_TIMEOUT;
}

#endif /* MOCK_HAL_H */
