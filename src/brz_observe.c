#include "brz_observe.h"
void bronze_event_observe(void* context, const BronzeEvent* e)
{ BronzeEventObserver* o=(BronzeEventObserver*)context; FILE* f=o&&o->stream?o->stream:stdout; if(o&&o->debug) fprintf(f,"{\"day\":%d,\"actor\":%u,\"place\":%d,\"action\":\"%s\",\"amount\":%.3f,\"result\":%d}\n",e->day,e->actor_id,e->settlement_id,e->subject?e->subject:"",e->amount,e->result); else fprintf(f,"Day %d: actor %u %s at place %d.\n",e->day,e->actor_id,e->subject?e->subject:"acted",e->settlement_id); }
