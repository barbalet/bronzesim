/*
 * BRONZESIM — brz_world.h
 *
 * Defines the conceptual world model.
 *
 * Responsibilities:
 *  - Global world constants (30mi × 30mi, 3ft resolution)
 *  - Terrain tags (coast, forest, field, hill, river, etc.)
 *  - Resource types (fish, grain, wood, clay, copper, tin)
 *  - World generation interfaces
 *
 * The world is NOT stored as a full grid.
 * Instead, it is accessed procedurally via chunk generation.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FEET_PER_MILE 5280
#define CELL_FEET 3

#define WORLD_MILES_X 30
#define WORLD_MILES_Y 30

#define WORLD_FEET_X (WORLD_MILES_X * FEET_PER_MILE)
#define WORLD_FEET_Y (WORLD_MILES_Y * FEET_PER_MILE)

#define WORLD_CELLS_X (WORLD_FEET_X / CELL_FEET)   // 52800
#define WORLD_CELLS_Y (WORLD_FEET_Y / CELL_FEET)   // 52800

// Terrain tags (bitfield)
enum
{
    TAG_COAST  = 1u<<0,
    TAG_BEACH  = 1u<<1,
    TAG_FOREST = 1u<<2,
    TAG_MARSH  = 1u<<3,
    TAG_HILL   = 1u<<4,
    TAG_RIVER  = 1u<<5,
    TAG_FIELD  = 1u<<6,
    TAG_SETTLE = 1u<<7
};

typedef enum
{
    RES_FISH=0,
    RES_GRAIN,
    RES_WOOD,
    RES_CLAY,
    RES_COPPER,
    RES_TIN,
    RES_FIRE,
    RES_PLANT_FIBER,
    RES_CATTLE,
    RES_SHEEP,
    RES_PIG,
    RES_CHARCOAL,
    RES_RELIGION,
    RES_NATIONALISM,
    RES_MAX
} ResourceKind;

typedef enum
{
    SEASON_SPRING=0,
    SEASON_SUMMER,
    SEASON_AUTUMN,
    SEASON_WINTER,
    SEASON_ANY=255
} SeasonKind;

typedef struct
{
    float renew_per_day[RES_MAX];  // 0..1; applied to loaded chunks
} ResourceModel;

typedef struct
{
    uint32_t seed;
    int settlement_count;
    ResourceModel res_model;
} WorldSpec;

typedef struct
{
    uint32_t seed;
} WorldGen;

void worldgen_init(WorldGen* g, uint32_t seed);

// Deterministic terrain tags
uint8_t world_cell_tags(WorldGen* g, int32_t x, int32_t y, int settlement_count);

// Deterministic initial density 0..255 based on tags + noise
uint8_t world_cell_res0(WorldGen* g, int32_t x, int32_t y, ResourceKind rk, uint8_t tags);

// Season helpers (360 day year)
SeasonKind world_season_kind(uint32_t day);
const char* world_season_name(SeasonKind s);
SeasonKind world_season_parse(const char* s);
