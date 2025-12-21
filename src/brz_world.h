#ifndef BRZ_WORLD_H
#define BRZ_WORLD_H

#include <stdint.h>
#include "brz_util.h"
#include "brz_kinds.h"

// World scale
#define CELL_FEET 3
// World dimensions in cells (must be > settlement placement margins)
#define WORLD_CELLS_X 8192
#define WORLD_CELLS_Y 8192

// Terrain tags (bitmask)
enum
{
    TAG_COAST  = 1u << 0,
    TAG_BEACH  = 1u << 1,
    TAG_FOREST = 1u << 2,
    TAG_FIELD  = 1u << 3,
    TAG_HILL   = 1u << 4,
    TAG_MARSH  = 1u << 5,
    TAG_RIVER  = 1u << 6,
    TAG_SETTLE = 1u << 7
};

typedef enum
{
    SEASON_SPRING = 0,
    SEASON_SUMMER = 1,
    SEASON_AUTUMN = 2,
    SEASON_WINTER = 3,
    SEASON_ANY = 4
} SeasonKind;

typedef int ResourceKindId;

typedef struct
{
    // 0..1; applied to loaded chunks
    float* renew_per_day; // length = resources.count
} ResourceModel;

typedef struct
{
    uint32_t seed;
    int settlement_count;

    // Dynamic resource kinds (defined by the DSL)
    KindTable resources;

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
uint8_t world_cell_res0(WorldGen* g, const WorldSpec* spec, int32_t x, int32_t y, ResourceKindId rk, uint8_t tags);

// Season helpers (360 day year)
SeasonKind world_season_kind(uint32_t day);
const char* world_season_name(SeasonKind s);
SeasonKind world_season_parse(const char* s);

#endif
