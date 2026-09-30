#ifndef BRZ_STATE_H
#define BRZ_STATE_H

#include "brz_agent.h"

typedef struct {
    const ParsedConfig* config;
    BrzWorld world;
    BrzSettlement* settlements;
    BrzAgent* agents;
    BronzeWorldPort world_port;
    BronzeSettlementPort settlement_port;
    BrzRng rng;
    int day, settlement_count, agent_count;
    size_t resource_count, item_count;
} BrzSimulationState;

int brz_state_init(BrzSimulationState* state, const ParsedConfig* config);
int brz_state_step(BrzSimulationState* state, const BronzeEventSink* events);
void brz_state_destroy(BrzSimulationState* state);
int brz_state_save(const BrzSimulationState* state, const char* path);
int brz_state_load(BrzSimulationState* state, const ParsedConfig* config, const char* path);

#endif
