#ifndef SCENARIO_RUNTIME_H
#define SCENARIO_RUNTIME_H

#include <stdint.h>

/* Small, scenario-neutral ABI shared by independent simulation adapters. */
#define SCENARIO_RUNTIME_VERSION 1u
#define SCENARIO_ID_NONE UINT32_MAX

typedef uint64_t ScenarioTick;
typedef uint32_t ScenarioActorId;
typedef uint32_t ScenarioPlaceId;

typedef enum {
    SCENARIO_RESULT_COMPLETED,
    SCENARIO_RESULT_UNAVAILABLE,
    SCENARIO_RESULT_DEFERRED,
    SCENARIO_RESULT_INVALID
} ScenarioResult;

typedef struct {
    uint32_t version;
    ScenarioTick tick;
    ScenarioActorId actor_id;
    ScenarioActorId counterpart_id;
    ScenarioPlaceId place_id;
    const char* action_id;
    double requested_amount;
    double completed_amount;
    ScenarioResult result;
} ScenarioEvent;

void scenario_event_init(ScenarioEvent* event, ScenarioTick tick,
                         ScenarioActorId actor_id, ScenarioActorId counterpart_id,
                         ScenarioPlaceId place_id, const char* action_id,
                         double requested_amount, double completed_amount,
                         ScenarioResult result);
int scenario_event_equivalent(const ScenarioEvent* left, const ScenarioEvent* right);

#endif
