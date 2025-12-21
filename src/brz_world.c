/*
 * BRONZESIM — brz_world.c
 *
 * Implements deterministic procedural world generation.
 *
 * Responsibilities:
 *  - Assign terrain tags to any requested cell
 *  - Generate initial resource densities per cell
 *  - Provide season calculation based on simulation day
 *
 * World generation is:
 *  - Stateless (pure functions based on coordinates + seed)
 *  - Deterministic (same seed → same world)
 *  - Lazy (cells only generated when needed)
 *
 * This allows a 52,800 × 52,800 cell world to exist
 * without allocating billions of cells.
 */

#include "brz_world.h"
#include "brz_util.h"
#include <string.h>

static bool is_coast_cell(int32_t x, int32_t y)
{
    return (x < 2) || (y < 2) || (x >= (int32_t)WORLD_CELLS_X-2) || (y >= (int32_t)WORLD_CELLS_Y-2);
}

static uint8_t noise01(WorldGen* g, int32_t x, int32_t y, uint32_t salt)
{
    uint32_t h = brz_hash_u32((uint32_t)x, (uint32_t)y, g->seed ^ salt);
    return (uint8_t)(h & 0xFF);
}

void worldgen_init(WorldGen* g, uint32_t seed)
{
    g->seed = seed;
}

uint8_t world_cell_tags(WorldGen* g, int32_t x, int32_t y, int settlement_count)
{
    (void)settlement_count;
    uint8_t tags = 0;

    if(is_coast_cell(x,y)) tags |= TAG_COAST;

    // beach near coast edge
    if(!(tags & TAG_COAST))
    {
        if(x<3 || y<3 || x>(int32_t)WORLD_CELLS_X-4 || y>(int32_t)WORLD_CELLS_Y-4)
        {
            if(noise01(g,x,y,0xBEEF1234u) < 140) tags |= TAG_BEACH;
        }
    }

    // forest/hill/marsh
    uint8_t n1 = noise01(g,x,y,0x1111A11Au);
    uint8_t n2 = noise01(g,x,y,0x2222B22Bu);
    uint8_t n3 = noise01(g,x,y,0x3333C33Cu);
    if(n1 > 150) tags |= TAG_FOREST;
    if(n2 > 200) tags |= TAG_HILL;
    if(n3 > 215) tags |= TAG_MARSH;

    // crude rivers
    uint8_t rv = noise01(g, x/8, y/8, 0x52A17B3Du);
    if(rv > 245) tags |= TAG_RIVER;

    // deterministic settlement clusters
    int32_t sx = (x/2000)*2000 + 1000;
    int32_t sy = (y/2000)*2000 + 1000;
    uint8_t sc = noise01(g, sx, sy, 0x5E771EADu);

    int32_t dx = x - sx, dy = y - sy;
    int32_t d2 = dx*dx + dy*dy;

    if(sc > 240 && d2 < (70*70))    tags |= TAG_SETTLE;
    if(sc > 240 && d2 < (250*250)) tags |= TAG_FIELD;

    return tags;
}

uint8_t world_cell_res0(WorldGen* g, const WorldSpec* spec, int32_t x, int32_t y, ResourceKindId rk, uint8_t tags)
{
    (void)spec;
    uint8_t base = noise01(g,x,y,0x9999DDDDu);
    const char* name = kind_table_name(&spec->resources, rk);

    if(strcmp(name,"fish")==0)
        return (tags & TAG_COAST)  ? brz_clamp_u8(120 + base/2) : 0;
    if(strcmp(name,"grain")==0)
        return (tags & TAG_FIELD)  ? brz_clamp_u8(80  + base/3) : 0;
    if(strcmp(name,"wood")==0)
        return (tags & TAG_FOREST) ? brz_clamp_u8(90  + base/3) : 0;
    if(strcmp(name,"clay")==0)
        return ((tags & TAG_RIVER) || (tags & TAG_MARSH)) ? brz_clamp_u8(60 + base/4) : 0;
    if(strcmp(name,"copper")==0)
        return (tags & TAG_HILL)   ? (base > 240 ? 40 : 5) : 0;
    if(strcmp(name,"tin")==0)
        return (tags & TAG_HILL)   ? (base > 245 ? 30 : 3) : 0;

    if(strcmp(name,"fire")==0)
        return (tags & TAG_FOREST) ? brz_clamp_u8(40 + base/5) : 0;
    if(strcmp(name,"plant_fiber")==0)
        return (tags & TAG_FIELD) ? brz_clamp_u8(45 + base/5) : 0;
    if(strcmp(name,"cattle")==0)
        return (tags & TAG_FIELD) ? brz_clamp_u8(40 + base/4) : 0;
    if(strcmp(name,"sheep")==0)
        return (tags & TAG_FIELD) ? brz_clamp_u8(35 + base/4) : 0;
    if(strcmp(name,"pig")==0)
        return (tags & TAG_FIELD) ? brz_clamp_u8(30 + base/4) : 0;
    if(strcmp(name,"charcoal")==0)
        return (tags & TAG_FOREST) ? brz_clamp_u8(25 + base/5) : 0;
    if(strcmp(name,"religion")==0)
        return (tags & TAG_SETTLE) ? brz_clamp_u8(60 + base/5) : 0;
    if(strcmp(name,"nationalism")==0)
        return (tags & TAG_SETTLE) ? brz_clamp_u8(20 + base/8) : 0;

    // Unknown resource kind: treat as not naturally occurring.
    return 0;
}

SeasonKind world_season_kind(uint32_t day)
{
    uint32_t d = day % 360u;
    if(d < 90u)  return SEASON_SPRING;
    if(d < 180u) return SEASON_SUMMER;
    if(d < 270u) return SEASON_AUTUMN;
    return SEASON_WINTER;
}

const char* world_season_name(SeasonKind s)
{
    switch(s)
    {
    case SEASON_SPRING:
        return "spring";
    case SEASON_SUMMER:
        return "summer";
    case SEASON_AUTUMN:
        return "autumn";
    case SEASON_WINTER:
        return "winter";
    default:
        return "any";
    }
}

SeasonKind world_season_parse(const char* s)
{
    if(!s) return SEASON_ANY;
    if(strcmp(s,"spring")==0) return SEASON_SPRING;
    if(strcmp(s,"summer")==0) return SEASON_SUMMER;
    if(strcmp(s,"autumn")==0) return SEASON_AUTUMN;
    if(strcmp(s,"winter")==0) return SEASON_WINTER;
    if(strcmp(s,"any")==0) return SEASON_ANY;
    return SEASON_ANY;
}
