/*
 * BRONZESIM — brz_sim.c
 *
 * Implements the simulation engine.
 *
 * Responsibilities:
 *  - Advance the simulation one day at a time
 *  - Evaluate vocation rules for each agent
 *  - Select tasks probabilistically
 *  - Execute generic task operations:
 *      move_to
 *      gather
 *      craft
 *      trade
 *      rest
 *  - Update hunger, fatigue, health, and age
 *  - Handle apprenticeship and vocation inheritance
 *
 * IMPORTANT DESIGN RULE:
 *  - NO occupation-specific logic lives here.
 *  - All behavior comes from the DSL.
 *
 * This file is the heart of BRONZESIM.
 */

#include "brz_sim.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t rng_u32(uint32_t seed, uint32_t a, uint32_t b, uint32_t c)
{
    return brz_hash_u32(a ^ seed, b, c);
}
static float rng_f01(uint32_t seed, uint32_t a, uint32_t b, uint32_t c)
{
    return (rng_u32(seed,a,b,c) & 0xFFFFFFu) / (float)0x1000000u;
}

/*
static void pick_spawn(WorldGen* g, int i, int32_t* out_x, int32_t* out_y)
{
    uint32_t h1 = brz_hash_u32((uint32_t)i, g->seed, 0xABCDEF01u);
    uint32_t h2 = brz_hash_u32((uint32_t)i, g->seed, 0xABCDEF02u);
    int32_t x = 200 + (int32_t)(h1 % (WORLD_CELLS_X-400));
    int32_t y = 200 + (int32_t)(h2 % (WORLD_CELLS_Y-400));
    *out_x = x;
    *out_y = y;
}
*/

static int nearest_settlement(const Sim* s, int32_t x, int32_t y)
{
    int best = 0;
    int64_t bestd = INT64_MAX;
    for(int i=0; i<s->settlement_count; i++)
    {
        int64_t dx = (int64_t)x - s->settlements[i].x;
        int64_t dy = (int64_t)y - s->settlements[i].y;
        int64_t d2 = dx*dx + dy*dy;
        if(d2 < bestd)
        {
            bestd = d2;
            best = i;
        }
    }
    return best;
}

static void step_toward(Agent* a, int32_t tx, int32_t ty)
{
    int dx = (tx > a->x) ? 1 : (tx < a->x ? -1 : 0);
    int dy = (ty > a->y) ? 1 : (ty < a->y ? -1 : 0);
    a->x = brz_clamp_i32(a->x + dx, 0, (int32_t)WORLD_CELLS_X-1);
    a->y = brz_clamp_i32(a->y + dy, 0, (int32_t)WORLD_CELLS_Y-1);
    a->fatigue += 0.004f;
}

static void roam(Sim* s, Agent* a, int steps)
{
    for(int i=0; i<steps; i++)
    {
        uint32_t h = rng_u32(s->world.seed, (uint32_t)(a->x+i*17), (uint32_t)(a->y+i*31), s->day);
        int dir = (int)(h % 4u);
        int dx=0, dy=0;
        if(dir==0) dx=1;
        else if(dir==1) dx=-1;
        else if(dir==2) dy=1;
        else dy=-1;
        a->x = brz_clamp_i32(a->x + dx, 0, (int32_t)WORLD_CELLS_X-1);
        a->y = brz_clamp_i32(a->y + dy, 0, (int32_t)WORLD_CELLS_Y-1);
        a->fatigue += 0.003f;
    }
}

// radius scan to find a cell with wanted tag (or best score)
static void move_to_tag(Sim* s, Agent* a, uint8_t want_tag, int radius)
{
    int bestScore = -999999;
    int32_t bestX = a->x, bestY = a->y;

    for(int dy=-radius; dy<=radius; dy++)
    {
        for(int dx=-radius; dx<=radius; dx++)
        {
            int32_t xx = a->x + dx;
            int32_t yy = a->y + dy;
            if(xx<0 || yy<0 || xx>=(int32_t)WORLD_CELLS_X || yy>=(int32_t)WORLD_CELLS_Y) continue;
            int idx=0;
            Chunk* ch = cache_get_cell(&s->cache, xx, yy, &idx);
            uint8_t tags = ch->terrain[idx];
            int score = 0;
            if(tags & want_tag) score += 50;
            score -= (dx*dx + dy*dy);
            if(score > bestScore)
            {
                bestScore = score;
                bestX = xx;
                bestY = yy;
            }
        }
    }
    a->x = bestX;
    a->y = bestY;
    a->fatigue += 0.01f;
}

static int gather(Sim* s, Agent* a, int rk_id, int want_units)
{
    int idx=0;
    Chunk* ch = cache_get_cell(&s->cache, a->x, a->y, &idx);
    uint8_t* d = &ch->res[(rk_id*CHUNK_CELLS)+idx];

    int avail_units = (*d > 0) ? (int)(*d / 32) : 0;
    int take = (avail_units < want_units) ? avail_units : want_units;

    int newd = (int)(*d) - take*20;
    *d = brz_clamp_u8(newd);
    return take;
}

static void eat(Sim* s, Agent* a)
{
    if(a->hunger <= 0.7f) return;

    int id_fish  = kind_table_find(&s->item_kinds, "fish");
    int id_grain = kind_table_find(&s->item_kinds, "grain");

    if(id_fish >= 0 && a->inv[id_fish] > 0)
    {
        a->inv[id_fish]--;
        a->hunger -= 0.35f;
    }
    if(a->hunger > 0.7f && id_grain >= 0 && a->inv[id_grain] > 0)
    {
        a->inv[id_grain]--;
        a->hunger -= 0.30f;
    }
    if(a->hunger < 0) a->hunger = 0;
}

// minimal crafting recipes (still stand-alone; occupations choose "craft X N")
static void craft_item(Sim* s, Agent* a, int item_id, int amount)
{
    const char* name = kind_table_name(&s->item_kinds, item_id);

    int id_clay   = kind_table_find(&s->item_kinds, "clay");
    int id_wood   = kind_table_find(&s->item_kinds, "wood");
    int id_copper = kind_table_find(&s->item_kinds, "copper");
    int id_tin    = kind_table_find(&s->item_kinds, "tin");
    int id_bronze = kind_table_find(&s->item_kinds, "bronze");
    int id_pot    = kind_table_find(&s->item_kinds, "pot");
    int id_tool   = kind_table_find(&s->item_kinds, "tool");

    for(int i=0; i<amount; i++)
    {
        if(strcmp(name,"pot")==0)
        {
            // 1 pot: 2 clay + 1 wood
            if(id_pot >= 0 && id_clay >= 0 && id_wood >= 0 &&
               a->inv[id_clay] >= 2 && a->inv[id_wood] >= 1)
            {
                a->inv[id_clay] -= 2;
                a->inv[id_wood] -= 1;
                a->inv[id_pot] += 1;
                a->fatigue += 0.01f;
            }
        }
        else if(strcmp(name,"bronze")==0)
        {
            // 1 bronze: 1 copper + 1 tin
            if(id_bronze >= 0 && id_copper >= 0 && id_tin >= 0 &&
               a->inv[id_copper] >= 1 && a->inv[id_tin] >= 1)
            {
                a->inv[id_copper] -= 1;
                a->inv[id_tin] -= 1;
                a->inv[id_bronze] += 1;
                a->fatigue += 0.02f;
            }
        }
        else if(strcmp(name,"tool")==0)
        {
            // 1 tool: 1 bronze
            if(id_tool >= 0 && id_bronze >= 0 && a->inv[id_bronze] >= 1)
            {
                a->inv[id_bronze] -= 1;
                a->inv[id_tool] += 1;
                a->fatigue += 0.02f;
            }
        }
        else
        {
            // Unknown craftable item; no-op (kept safe for custom DSL extensions)
        }
    }
}

// trade with household settlement (simple value-based barter)
static void trade(Sim* s, Agent* a)
{
    Household* hh = &s->households[a->household_id];
    Settlement* st = &s->settlements[hh->settlement_id];

    // Convert surplus into deficits using a small set of barters.
    // Kinds are dynamic, so we resolve ids by name at runtime.
    int want_ids[4];
    want_ids[0] = kind_table_find(&s->item_kinds, "grain");
    want_ids[1] = kind_table_find(&s->item_kinds, "fish");
    want_ids[2] = kind_table_find(&s->item_kinds, "tool");
    want_ids[3] = kind_table_find(&s->item_kinds, "pot");

    for(int wi=0; wi<4; wi++)
    {
        int want = want_ids[wi];
        if(want < 0) continue;
        if(a->inv[want] >= 3) continue; // not lacking

        // find something to offer with high local value & surplus
        int offer_default = kind_table_find(&s->item_kinds, "fish");
        int offer = (offer_default >= 0) ? offer_default : 0;

        int bestScore = -999999;
        for(int it=0; it<s->item_kind_count; it++)
        {
            if(it == want) continue;
            if(a->inv[it] <= 2) continue; // not enough surplus
            // score = value * surplus
            int score = (int)(st->val[it] * 100.0f) + a->inv[it] * 10;
            if(score > bestScore)
            {
                bestScore = score;
                offer = it;
            }
        }

        if(bestScore <= -999990) continue;

        // execute barter: give 1 offer for 1 want (toy model)
        a->inv[offer] -= 1;
        a->inv[want]  += 1;
        a->fatigue += 0.005f;
    }
}

static bool cond_eval(const Condition* c, const Sim* s, const Agent* a, float roll)
{
    if(c->has_hunger && !(a->hunger > c->hunger_threshold)) return false;
    if(c->has_fatigue && !(a->fatigue < c->fatigue_threshold)) return false;
    if(c->season_eq != SEASON_ANY)
    {
        if(world_season_kind(s->day) != c->season_eq) return false;
    }
    for(int k=0; k<c->inv_count; k++)
    {
        int have = a->inv[c->inv_item[k]];
        int v = c->inv_value[k];
        switch(c->inv_cmp[k])
        {
        case CMP_GT:
            if(!(have > v)) return false;
            break;
        case CMP_LT:
            if(!(have < v)) return false;
            break;
        case CMP_GE:
            if(!(have >= v)) return false;
            break;
        case CMP_LE:
            if(!(have <= v)) return false;
            break;
        default:
            break;
        }
    }
    if(c->has_prob)
    {
        if(!(roll < c->prob)) return false;
    }
    return true;
}

static const TaskDef* choose_task(const Sim* s, const Agent* a)
{
    const VocationDef* voc = voc_get(&s->voc_table, a->vocation_id);
    if(!voc) return NULL;

    int total = 0;
    // deterministic per agent/day probability roll used in conditions
    float roll = rng_f01(s->world.seed, (uint32_t)a->x, (uint32_t)a->y, s->day ^ (uint32_t)(a->household_id*131));

    for(int i=0; i<voc->rule_count; i++)
    {
        const RuleDef* r = &voc->rules[i];
        if(cond_eval(&r->cond, s, a, roll))
        {
            total += (r->weight > 0 ? r->weight : 0);
        }
    }
    if(total <= 0) return NULL;

    int pick = (int)(rng_u32(s->world.seed, (uint32_t)a->x, (uint32_t)a->y, s->day ^ 0xA11CEu) % (uint32_t)total);
    for(int i=0; i<voc->rule_count; i++)
    {
        const RuleDef* r = &voc->rules[i];
        if(cond_eval(&r->cond, s, a, roll))
        {
            int w = (r->weight > 0 ? r->weight : 0);
            pick -= w;
            if(pick < 0)
            {
                return voc_task(voc, r->task_name);
            }
        }
    }
    return NULL;
}

static void exec_task(Sim* s, Agent* a, const TaskDef* t)
{
    if(!t) return;
    for(int i=0; i<t->op_count; i++)
    {
        const int kind = t->ops[i].kind;
        switch(kind)
        {
        case OP_MOVE_TO:
            move_to_tag(s, a, (uint8_t)t->ops[i].arg_j, 12);
            break;
        case OP_GATHER:
{
    int rk_id = t->ops[i].arg_j;
    int got = gather(s, a, rk_id, t->ops[i].arg_i);

    // By convention, "gather <resource>" deposits into an item with the same name,
    // if that item exists in the DSL-declared item kinds.
    const char* rname = kind_table_name(&s->world.resources, rk_id);
    int item_id = kind_table_find(&s->item_kinds, rname);
    if(item_id >= 0)
        a->inv[item_id] += got;
}
break;
        case OP_CRAFT:
            craft_item(s, a, t->ops[i].arg_j, t->ops[i].arg_i);
            break;
        case OP_TRADE:
            trade(s, a);
            break;
        case OP_REST:
            a->fatigue -= 0.2f;
            if(a->fatigue < 0) a->fatigue = 0;
            break;
        case OP_ROAM:
            roam(s, a, t->ops[i].arg_i);
            break;
        default:
            break;
        }
    }
}

static void apprenticeship(Sim* s, Agent* a)
{
    if(a->age < 10 || a->age > 16) return;
    Household* hh = &s->households[a->household_id];
    if(hh->parent_id < 0) return;
    int pid = hh->parent_id;
    if(pid < 0 || pid >= s->agent_count) return;
    int pv = s->agents[pid].vocation_id;
    float r = rng_f01(s->world.seed, (uint32_t)a->x, (uint32_t)a->y, s->day ^ 0xA22E11u);
    if(r < 0.10f)
    {
        a->vocation_id = pv;
    }
}

static void role_switching(Sim* s)
{
    // Every switch_every_days: compute scarcity and nudge adult agents.
    if(s->switch_every_days == 0) return;
    if((s->day % s->switch_every_days) != 0) return;

    long* totals = (long*)calloc((size_t)s->item_kind_count, sizeof(long));
    BRZ_ASSERT(totals != NULL);
    int alive=0;
    for(int i=0; i<s->agent_count; i++)
    {
        if(s->agents[i].health <= 0.0f) continue;
        alive++;
        for(int it=0; it<s->item_kind_count; it++) totals[it] += s->agents[i].inv[it];
    }
    if(alive <= 0) return;

    // scarcity: per-capita target
    int id_grain = kind_table_find(&s->item_kinds, "grain");
int id_fish  = kind_table_find(&s->item_kinds, "fish");
int id_tool  = kind_table_find(&s->item_kinds, "tool");
int id_pot   = kind_table_find(&s->item_kinds, "pot");

float pc_grain = (id_grain>=0) ? ((float)totals[id_grain] / (float)alive) : 0.0f;
float pc_fish  = (id_fish>=0)  ? ((float)totals[id_fish]  / (float)alive) : 0.0f;
float pc_tool  = (id_tool>=0)  ? ((float)totals[id_tool]  / (float)alive) : 0.0f;
float pc_pot   = (id_pot>=0)   ? ((float)totals[id_pot]   / (float)alive) : 0.0f;
const int farmer_id = voc_find(&s->voc_table, "farmer");
    const int fisher_id = voc_find(&s->voc_table, "fisher");
    const int smith_id  = voc_find(&s->voc_table, "smith");
    const int potter_id = voc_find(&s->voc_table, "potter");

    // Choose desired voc based on biggest deficit
    int target_voc = -1;
    if(farmer_id >= 0 && pc_grain < 3.0f) target_voc = farmer_id;
    if(fisher_id >= 0 && pc_fish  < 2.0f && (target_voc<0 || pc_fish < pc_grain)) target_voc = fisher_id;
    if(smith_id  >= 0 && pc_tool  < 0.6f) target_voc = smith_id;
    if(potter_id >= 0 && pc_pot   < 0.6f) target_voc = potter_id;

    if(target_voc < 0) return;

    // Switch a few adults who are not parents and not already in target vocation.
    int switched=0;
    for(int i=0; i<s->agent_count; i++)
    {
        Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;
        if(a->age < 17) continue;
        if(a->vocation_id == target_voc) continue;

        // don't switch household parent (stability)
        Household* hh = &s->households[a->household_id];
        if(hh->parent_id == i) continue;

        float r = rng_f01(s->world.seed, (uint32_t)a->x, (uint32_t)a->y, s->day ^ 0x5A17C9u);
        if(r < 0.05f)
        {
            a->vocation_id = target_voc;
            switched++;
            if(switched >= (alive/50 + 1)) break; // ~2% per switch cycle
        }
    }

    if(switched > 0)
    {
        const VocationDef* v = voc_get(&s->voc_table, target_voc);
        fprintf(stdout, "Day %u: role switching nudged %d adults into vocation '%s'\n",
                s->day, switched, v ? v->name : "?");
    }

}


void sim_init(Sim* s, ParsedConfig* cfg)
{
    BRZ_ASSERT(s != NULL && cfg != NULL);
    memset(s,0,sizeof(*s));

    // Move (steal) dynamic kinds + vocations out of the parsed config so there
    // is a single owner and we avoid double-free.
    s->world.seed = cfg->seed;
    s->world.settlement_count = cfg->settlement_count;

    s->world.resources = cfg->resources;
    kind_table_init(&cfg->resources); // cfg now empty; ownership moved

    s->world.res_model.renew_per_day = cfg->renew_per_day;
    cfg->renew_per_day = NULL;

    s->item_kinds = cfg->items;
    kind_table_init(&cfg->items);

    s->item_kind_count = s->item_kinds.count;

    s->voc_table = cfg->voc_table;
    memset(&cfg->voc_table, 0, sizeof(cfg->voc_table));

    worldgen_init(&s->gen, s->world.seed);

    // Build cache
    uint32_t cache_max = (cfg->cache_max > 16) ? cfg->cache_max : 16;
    cache_init(&s->cache, 4096, cache_max, &s->gen, &s->world);

    // Settlements
    s->settlement_count = (s->world.settlement_count > 1) ? s->world.settlement_count : 1;
    s->settlements = (Settlement*)calloc((size_t)s->settlement_count, sizeof(Settlement));
    BRZ_ASSERT(s->settlements != NULL);

    for(int i=0; i<s->settlement_count; i++)
    {
        s->settlements[i].val = (float*)calloc((size_t)s->item_kind_count, sizeof(float));
        BRZ_ASSERT(s->settlements[i].val != NULL);

        uint32_t h1 = brz_hash_u32((uint32_t)i, s->world.seed, 0x5E77A11Au);
        uint32_t h2 = brz_hash_u32((uint32_t)i, s->world.seed, 0x5E77B22Bu);
        int32_t x = 500 + (int32_t)(h1 % (WORLD_CELLS_X-1000));
        int32_t y = 500 + (int32_t)(h2 % (WORLD_CELLS_Y-1000));
        s->settlements[i].x = x;
        s->settlements[i].y = y;

        // Assign a simple value weight per item with per-settlement variation.
        for(int it=0; it<s->item_kind_count; it++)
        {
            uint32_t hh = brz_hash_u32((uint32_t)it, (uint32_t)i, s->world.seed ^ 0xBEEFBEEFu);
            float base = 0.5f + (float)(hh % 1000) / 1000.0f;
            s->settlements[i].val[it] = base;
        }
    }

    // Households
    s->household_count = cfg->agent_count / 6;
    if(s->household_count < 1) s->household_count = 1;
    s->households = (Household*)calloc((size_t)s->household_count, sizeof(Household));
    BRZ_ASSERT(s->households != NULL);
    for(int i=0; i<s->household_count; i++)
    {
        s->households[i].id = i;
        s->households[i].settlement_id = (i % s->settlement_count);
        s->households[i].parent_id = -1;
    }

    // Agents
    s->agent_count = (cfg->agent_count > 1) ? cfg->agent_count : 1;
    s->agents = (Agent*)calloc((size_t)s->agent_count, sizeof(Agent));
    BRZ_ASSERT(s->agents != NULL);

    for(int i=0; i<s->agent_count; i++)
    {
        Agent* a = &s->agents[i];
        memset(a,0,sizeof(*a));
        a->inv = (int*)calloc((size_t)s->item_kind_count, sizeof(int));
        BRZ_ASSERT(a->inv != NULL);

        int hid = (i % s->household_count);
        a->household_id = hid;

        Household* hh = &s->households[hid];        // parent: assign first adult in household
        if(hh->parent_id < 0 && (i % 6)==0) hh->parent_id = i;

        Settlement* st = &s->settlements[hh->settlement_id];

        uint32_t h1 = brz_hash_u32((uint32_t)i, s->world.seed, 0xC0FFEE11u);
        uint32_t h2 = brz_hash_u32((uint32_t)i, s->world.seed, 0xC0FFEE22u);

        a->x = st->x + (int32_t)(h1 % 200) - 100;
        a->y = st->y + (int32_t)(h2 % 200) - 100;
        a->age = (int)(h1 % 45);

        a->vocation_id = (s->voc_table.vocation_count>0) ? (i % s->voc_table.vocation_count) : 0;

        a->hunger = 0.1f + 0.2f * ((h2 % 1000) / 1000.0f);
        a->fatigue = 0.1f + 0.2f * ((h1 % 1000) / 1000.0f);
        a->health = 1.0f;
    }

    s->day = 0;
    s->switch_every_days = 60;
}

void sim_destroy(Sim* s)
{
    if(!s) return;

    for(int i=0; i<s->agent_count; i++)
        free(s->agents[i].inv);
    free(s->agents);
    s->agents = NULL;

    for(int i=0; i<s->settlement_count; i++)
        free(s->settlements[i].val);
    free(s->settlements);
    s->settlements = NULL;

    free(s->households);
    s->households = NULL;

    cache_destroy(&s->cache);

    voc_table_destroy(&s->voc_table);
    kind_table_destroy(&s->item_kinds);
    kind_table_destroy(&s->world.resources);
    free(s->world.res_model.renew_per_day);
    s->world.res_model.renew_per_day = NULL;

    memset(s,0,sizeof(*s));
}

void sim_step(Sim* s)
{
    s->day++;

    SeasonKind season = world_season_kind(s->day);

    // world regen for loaded chunks
    cache_regen_loaded(&s->cache, season);

    for(int i=0; i<s->agent_count; i++)
    {
        Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;

        if((s->day % 360u)==0) a->age++;

        // needs drift
        a->hunger += 0.18f;
        if(a->hunger > 1.0f) a->hunger = 1.0f;
        a->fatigue -= 0.08f;
        if(a->fatigue < 0.0f) a->fatigue = 0.0f;

        eat(s,a);
        apprenticeship(s,a);

        // starvation damage
        if(a->hunger > 0.95f)
        {
            a->health -= 0.01f;
            if(a->health < 0) a->health = 0;
            continue;
        }

        // if exhausted, rest
        if(a->fatigue >= 0.90f)
        {
            a->fatigue -= 0.20f;
            if(a->fatigue < 0) a->fatigue = 0;
            continue;
        }

        // generic occupation execution
        const TaskDef* t = choose_task(s, a);
        if(!t)
        {
            // fallback: wander a bit and/or trade toward settlement
            int sid = nearest_settlement(s, a->x, a->y);
            step_toward(a, s->settlements[sid].x, s->settlements[sid].y);
            if((i % 9)==0) trade(s,a);
        }
        else
        {
            exec_task(s,a,t);
        }
    }

    role_switching(s);
}

void sim_report(const Sim* s)
{
    long* inv = (long*)calloc((size_t)s->item_kind_count, sizeof(long));
    BRZ_ASSERT(inv != NULL);
    int voc_counts[MAX_VOCATIONS]= {0};
    int alive=0;

    for(int i=0; i<s->agent_count; i++)
    {
        const Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;
        alive++;
        for(int it=0; it<s->item_kind_count; it++) inv[it] += a->inv[it];
        if(a->vocation_id >=0 && a->vocation_id < s->voc_table.vocation_count)
        {
            voc_counts[a->vocation_id]++;
        }
    }

    printf("Day %u season=%s alive=%d cache_chunks=%u\n",
       s->day, world_season_name(world_season_kind(s->day)), alive, s->cache.live_chunks);

// Selected per-capita inventory totals (only if the kind exists in the DSL)
{
    int id_fish   = kind_table_find(&s->item_kinds, "fish");
    int id_grain  = kind_table_find(&s->item_kinds, "grain");
    int id_wood   = kind_table_find(&s->item_kinds, "wood");
    int id_clay   = kind_table_find(&s->item_kinds, "clay");
    int id_copper = kind_table_find(&s->item_kinds, "copper");
    int id_tin    = kind_table_find(&s->item_kinds, "tin");
    int id_bronze = kind_table_find(&s->item_kinds, "bronze");
    int id_tool   = kind_table_find(&s->item_kinds, "tool");
    int id_pot    = kind_table_find(&s->item_kinds, "pot");

    long c_fish   = (id_fish>=0)   ? inv[id_fish]   : 0;
    long c_grain  = (id_grain>=0)  ? inv[id_grain]  : 0;
    long c_wood   = (id_wood>=0)   ? inv[id_wood]   : 0;
    long c_clay   = (id_clay>=0)   ? inv[id_clay]   : 0;
    long c_copper = (id_copper>=0) ? inv[id_copper] : 0;
    long c_tin    = (id_tin>=0)    ? inv[id_tin]    : 0;
    long c_bronze = (id_bronze>=0) ? inv[id_bronze] : 0;
    long c_tool   = (id_tool>=0)   ? inv[id_tool]   : 0;
    long c_pot    = (id_pot>=0)    ? inv[id_pot]    : 0;

    printf("  inv_totals: fish=%ld grain=%ld wood=%ld clay=%ld copper=%ld tin=%ld bronze=%ld tool=%ld pot=%ld\n",
           c_fish, c_grain, c_wood, c_clay, c_copper, c_tin, c_bronze, c_tool, c_pot);
}

    printf("  vocations:");
    for(int v=0; v<s->voc_table.vocation_count; v++)
    {
        printf(" %s=%d", s->voc_table.vocations[v].name, voc_counts[v]);
    }
    printf("\n");
    free(inv);
}

void sim_write_snapshot_json(const Sim* s, const char* path)
{
    FILE* f = fopen(path,"wb");
    if(!f) return;

    long* inv = (long*)calloc((size_t)s->item_kind_count, sizeof(long));
    BRZ_ASSERT(inv != NULL);
    int alive=0;
    int voc_counts[MAX_VOCATIONS]= {0};

    for(int i=0; i<s->agent_count; i++)
    {
        const Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;
        alive++;
        for(int it=0; it<s->item_kind_count; it++) inv[it] += a->inv[it];
        if(a->vocation_id >=0 && a->vocation_id < s->voc_table.vocation_count) voc_counts[a->vocation_id]++;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"day\": %u,\n", s->day);
    fprintf(f, "  \"season\": \"%s\",\n", world_season_name(world_season_kind(s->day)));
    fprintf(f, "  \"alive\": %d,\n", alive);
    fprintf(f, "  \"cache_chunks\": %u,\n", s->cache.live_chunks);

    
fprintf(f, "  \"inventory\": {\n");
for(int it=0; it<s->item_kind_count; it++)
{
    const char* nm = kind_table_name(&s->item_kinds, it);
    fprintf(f, "    \"%s\": %ld%s\n", nm, inv[it], (it==(s->item_kind_count-1) ? "" : ","));
}
fprintf(f, "  },\n");

    fprintf(f, "  \"vocations\": {\n");
    for(int v=0; v<s->voc_table.vocation_count; v++)
    {
        fprintf(f, "    \"%s\": %d%s\n", s->voc_table.vocations[v].name, voc_counts[v],
                (v==s->voc_table.vocation_count-1) ? "" : ",");
    }
    fprintf(f, "  }\n");

    fprintf(f, "}\n");
    free(inv);
    fclose(f);
}

void sim_dump_ascii_map(const Sim* s, const char* path, int w, int h)
{
    FILE* f = fopen(path,"wb");
    if(!f) return;

    // sample the world at w*h points; show terrain chars
    for(int yy=0; yy<h; yy++)
    {
        for(int xx=0; xx<w; xx++)
        {
            int32_t wx = (int32_t)((int64_t)xx * (WORLD_CELLS_X-1) / (w-1));
            int32_t wy = (int32_t)((int64_t)yy * (WORLD_CELLS_Y-1) / (h-1));
            int idx=0;
            Chunk* ch = cache_get_cell((ChunkCache*)&s->cache, wx, wy, &idx);
            uint8_t t = ch->terrain[idx];
            char c='.';
            if(t & TAG_COAST) c='~';
            else if(t & TAG_RIVER) c='=';
            else if(t & TAG_SETTLE) c='@';
            else if(t & TAG_FIELD) c=':';
            else if(t & TAG_FOREST) c='^';
            else if(t & TAG_HILL) c='A';
            else if(t & TAG_MARSH) c=',';
            fputc(c,f);
        }
        fputc('\n',f);
    }
    fclose(f);
}
