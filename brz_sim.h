/*
 * BRONZESIM — brz_sim.h
 *
 * Declares the core simulation data structures.
 *
 * Includes:
 *  - Agent definition
 *  - Settlement definition
 *  - Household / kinship model
 *  - Vocation and task runtime state
 *
 * This header describes WHAT exists in the simulation,
 * not HOW it behaves.
 */

#pragma once
#include <stdint.h>
#include "brz_world.h"
#include "brz_cache.h"
#include "brz_dsl.h"

typedef struct
{
    int id;
    int settlement_id;
    int parent_id;     // agent id
} Household;

typedef struct
{
    int32_t x,y;
    float val[ITEM_MAX]; // value weight per item for trading
} Settlement;

typedef struct
{
    int32_t x, y;
    int vocation_id;
    int age;             // years
    int household_id;

    int inv[ITEM_MAX];

    float hunger;        // 0..1
    float fatigue;       // 0..1
    float health;        // 0..1
} Agent;

typedef struct
{
    WorldSpec world;
    WorldGen gen;
    ChunkCache cache;

    Settlement* settlements;
    int settlement_count;

    Household* households;
    int household_count;

    Agent* agents;
    int agent_count;

    VocationTable voc_table;

    uint32_t day;

    // role switching tuning
    uint32_t switch_every_days;
} Sim;

void sim_init(Sim* s, const WorldSpec* spec, uint32_t cache_max, int agent_count, const VocationTable* vt);
void sim_destroy(Sim* s);

void sim_step(Sim* s);

void sim_report(const Sim* s);
void sim_write_snapshot_json(const Sim* s, const char* path);
void sim_dump_ascii_map(const Sim* s, const char* path, int w, int h);
