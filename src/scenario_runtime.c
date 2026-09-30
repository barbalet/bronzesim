#include "scenario_runtime.h"
#include <string.h>

void scenario_event_init(ScenarioEvent* event, ScenarioTick tick,
                         ScenarioActorId actor_id, ScenarioActorId counterpart_id,
                         ScenarioPlaceId place_id, const char* action_id,
                         double requested_amount, double completed_amount,
                         ScenarioResult result)
{
    if(!event) return;
    event->version=SCENARIO_RUNTIME_VERSION;
    event->tick=tick;
    event->actor_id=actor_id;
    event->counterpart_id=counterpart_id;
    event->place_id=place_id;
    event->action_id=action_id;
    event->requested_amount=requested_amount;
    event->completed_amount=completed_amount;
    event->result=result;
}

int scenario_event_equivalent(const ScenarioEvent* left, const ScenarioEvent* right)
{
    if(!left || !right || left->version!=SCENARIO_RUNTIME_VERSION ||
       right->version!=SCENARIO_RUNTIME_VERSION) return 0;
    return left->tick==right->tick && left->actor_id==right->actor_id &&
           left->counterpart_id==right->counterpart_id && left->place_id==right->place_id &&
           left->requested_amount==right->requested_amount &&
           left->completed_amount==right->completed_amount && left->result==right->result &&
           ((!left->action_id && !right->action_id) ||
            (left->action_id && right->action_id && strcmp(left->action_id,right->action_id)==0));
}
