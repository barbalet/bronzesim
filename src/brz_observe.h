#ifndef BRZ_OBSERVE_H
#define BRZ_OBSERVE_H
#include "brz_port.h"
#include <stdio.h>
typedef struct { FILE* stream; int debug; } BronzeEventObserver;
void bronze_event_observe(void* context, const BronzeEvent* event);
#endif
