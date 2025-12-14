/*
 * BRONZESIM — brz_parser.h
 *
 * Defines the structures produced by parsing the BRONZESIM DSL.
 *
 * This file describes:
 *  - ParsedConfig (global simulation settings)
 *  - Parsed vocations, tasks, and rules
 *
 * The parser converts human-readable DSL text into
 * strongly-typed C structures that the simulation can execute.
 *
 * IMPORTANT:
 *  - The parser performs validation but does not enforce behavior.
 *  - Missing or malformed data is handled defensively.
 *
 * The parser does NOT execute anything.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "brz_dsl.h"

typedef struct
{
    uint32_t seed;
    int days;
    int agent_count;
    int settlement_count;
    uint32_t cache_max;

    // resource renew per day (0..1 scale; applied to loaded chunks)
    float fish_renew;
    float grain_renew;
    float wood_renew;
    float clay_renew;
    float copper_renew;
    float tin_renew;


    float fire_renew;
    float plant_fiber_renew;
    float cattle_renew;
    float sheep_renew;
    float pig_renew;
    float charcoal_renew;
    float religion_renew;
    float nationalism_renew;
    // scriptable vocations
    VocationTable voc_table;

    // output options
    int snapshot_every_days;   // 0 disables
    int map_every_days;        // 0 disables
} ParsedConfig;

bool brz_parse_file(const char* path, ParsedConfig* out_cfg);
