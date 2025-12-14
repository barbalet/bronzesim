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

static void pick_spawn(WorldGen* g, int i, int32_t* out_x, int32_t* out_y)
{
    uint32_t h1 = brz_hash_u32((uint32_t)i, g->seed, 0xABCDEF01u);
    uint32_t h2 = brz_hash_u32((uint32_t)i, g->seed, 0xABCDEF02u);
    int32_t x = 200 + (int32_t)(h1 % (WORLD_CELLS_X-400));
    int32_t y = 200 + (int32_t)(h2 % (WORLD_CELLS_Y-400));
    *out_x = x;
    *out_y = y;
}

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

static int gather(Sim* s, Agent* a, ResourceKind rk, int want_units)
{
    int idx=0;
    Chunk* ch = cache_get_cell(&s->cache, a->x, a->y, &idx);
    uint8_t* d = &ch->res[rk][idx];

    int avail_units = (*d > 0) ? (int)(*d / 32) : 0;
    int take = (avail_units < want_units) ? avail_units : want_units;

    int newd = (int)(*d) - take*20;
    *d = brz_clamp_u8(newd);
    return take;
}

static void eat(Agent* a)
{
    if(a->hunger <= 0.7f) return;
    if(a->inv[ITEM_FISH] > 0)
    {
        a->inv[ITEM_FISH]--;
        a->hunger -= 0.35f;
    }
    if(a->hunger > 0.7f && a->inv[ITEM_GRAIN] > 0)
    {
        a->inv[ITEM_GRAIN]--;
        a->hunger -= 0.30f;
    }
    if(a->hunger < 0) a->hunger = 0;
}

// minimal crafting recipes (still stand-alone; occupations choose "craft X N")
static void craft_item(Agent* a, ItemKind item, int amount)
{
    for(int i=0; i<amount; i++)
    {
        switch(item)
        {
        case ITEM_POT:
            // 1 pot: 2 clay + 1 wood
            if(a->inv[ITEM_CLAY] >= 2 && a->inv[ITEM_WOOD] >= 1)
            {
                a->inv[ITEM_CLAY] -= 2;
                a->inv[ITEM_WOOD] -= 1;
                a->inv[ITEM_POT] += 1;
                a->fatigue += 0.01f;
            }
            break;
        case ITEM_BRONZE:
            // 1 bronze: 1 copper + 1 tin + 2 wood
            if(a->inv[ITEM_COPPER] >= 1 && a->inv[ITEM_TIN] >= 1 && a->inv[ITEM_WOOD] >= 2)
            {
                a->inv[ITEM_COPPER] -= 1;
                a->inv[ITEM_TIN] -= 1;
                a->inv[ITEM_WOOD] -= 2;
                a->inv[ITEM_BRONZE] += 1;
                a->fatigue += 0.02f;
            }
            break;
        case ITEM_TOOL:
            // 1 tool: 1 bronze
            if(a->inv[ITEM_BRONZE] >= 1)
            {
                a->inv[ITEM_BRONZE] -= 1;
                a->inv[ITEM_TOOL] += 1;
                a->fatigue += 0.02f;
            }
            break;
        default:
            // allow "craft wood" etc? no.
            break;
        }
    }
}

// trade with household settlement (simple value-based barter)
static void trade(Sim* s, Agent* a)
{
    Household* hh = &s->households[a->household_id];
    Settlement* st = &s->settlements[hh->settlement_id];

    // Convert surplus into deficits using a small set of barters.
    // If you want richer markets, extend this to an order book / prices.
    ItemKind wants[4] = { ITEM_GRAIN, ITEM_FISH, ITEM_TOOL, ITEM_POT };

    for(int wi=0; wi<4; wi++)
    {
        ItemKind want = wants[wi];
        if(a->inv[want] >= 3) continue; // not lacking

        // find something to offer with high local value & surplus
        ItemKind offer = ITEM_FISH;
        int bestScore = -999999;
        for(int it=0; it<ITEM_MAX; it++)
        {
            if(it==(int)want) continue;
            if(a->inv[it] < 6) continue;
            int score = (int)(st->val[it] * 100.0f);
            if(score > bestScore)
            {
                bestScore = score;
                offer = (ItemKind)it;
            }
        }

        if(a->inv[offer] < 6) continue;

        // trade 2 offer for 1 want if offer is valued >= want at this settlement
        if(st->val[offer] >= st->val[want])
        {
            a->inv[offer] -= 2;
            a->inv[want] += 1;
            a->fatigue += 0.01f;
        }
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
            ResourceKind rk = (ResourceKind)t->ops[i].arg_j;
            int got = gather(s, a, rk, t->ops[i].arg_i);
            // map resources -> items
            switch(rk)
            {
            case RES_FISH:
                a->inv[ITEM_FISH]   += got;
                break;
            case RES_GRAIN:
                a->inv[ITEM_GRAIN]  += got;
                break;
            case RES_WOOD:
                a->inv[ITEM_WOOD]   += got;
                break;
            case RES_CLAY:
                a->inv[ITEM_CLAY]   += got;
                break;
            case RES_COPPER:
                a->inv[ITEM_COPPER] += got;
                break;
            case RES_TIN:
                a->inv[ITEM_TIN]    += got;
                break;
            default:
                break;
            }
        }
        break;
        case OP_CRAFT:
            craft_item(a, (ItemKind)t->ops[i].arg_j, t->ops[i].arg_i);
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

    long totals[ITEM_MAX]= {0};
    int alive=0;
    for(int i=0; i<s->agent_count; i++)
    {
        if(s->agents[i].health <= 0.0f) continue;
        alive++;
        for(int it=0; it<ITEM_MAX; it++) totals[it] += s->agents[i].inv[it];
    }
    if(alive <= 0) return;

    // scarcity: per-capita target
    float pc_grain = (float)totals[ITEM_GRAIN] / (float)alive;
    float pc_fish  = (float)totals[ITEM_FISH]  / (float)alive;
    float pc_tool  = (float)totals[ITEM_TOOL]  / (float)alive;
    float pc_pot   = (float)totals[ITEM_POT]   / (float)alive;

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

void sim_init(Sim* s, const WorldSpec* spec, uint32_t cache_max, int agent_count, const VocationTable* vt)
{
    memset(s,0,sizeof(*s));
    s->world = *spec;
    worldgen_init(&s->gen, s->world.seed);

    s->voc_table = *vt;

    s->settlement_count = (s->world.settlement_count > 1) ? s->world.settlement_count : 1;
    s->settlements = (Settlement*)calloc((size_t)s->settlement_count, sizeof(Settlement));
    BRZ_ASSERT(s->settlements != NULL);

    for(int i=0; i<s->settlement_count; i++)
    {
        uint32_t h1 = brz_hash_u32((uint32_t)i, s->world.seed, 0x5E77A11Au);
        uint32_t h2 = brz_hash_u32((uint32_t)i, s->world.seed, 0x5E77B22Bu);
        int32_t x = 500 + (int32_t)(h1 % (WORLD_CELLS_X-1000));
        int32_t y = 500 + (int32_t)(h2 % (WORLD_CELLS_Y-1000));
        s->settlements[i].x = x;
        s->settlements[i].y = y;

        // Set item values with per-settlement variation (simple market differences)
        for(int it=0; it<ITEM_MAX; it++) s->settlements[i].val[it] = 1.0f;

        float r = (float)((brz_hash_u32((uint32_t)i, s->world.seed, 0xC0DEu) % 100u) / 100.0);
        s->settlements[i].val[ITEM_FISH]  = 1.0f + 0.5f*r;
        s->settlements[i].val[ITEM_GRAIN] = 1.0f + 0.5f*(1.0f-r);
        s->settlements[i].val[ITEM_POT]   = 1.0f + 0.4f*r;
        s->settlements[i].val[ITEM_TOOL]  = 1.2f + 0.6f*r;
        s->settlements[i].val[ITEM_BRONZE]= 1.3f + 0.7f*r;
    }

    s->agent_count = (agent_count > 1) ? agent_count : 1;
    s->household_count = (s->agent_count + 4) / 5;
    s->households = (Household*)calloc((size_t)s->household_count, sizeof(Household));
    BRZ_ASSERT(s->households != NULL);

    for(int i=0; i<s->household_count; i++)
    {
        s->households[i].id = i;
        s->households[i].settlement_id = i % s->settlement_count;
        s->households[i].parent_id = -1;
    }

    s->agents = (Agent*)calloc((size_t)s->agent_count, sizeof(Agent));
    BRZ_ASSERT(s->agents != NULL);

    // assign vocations by name fallback
    int default_voc = (vt->vocation_count > 0) ? 0 : -1;
    int farmer_id = voc_find(vt, "farmer");
    int fisher_id = voc_find(vt, "fisher");
    int potter_id = voc_find(vt, "potter");
    int smith_id  = voc_find(vt, "smith");
    int trader_id = voc_find(vt, "trader");

    for(int i=0; i<s->agent_count; i++)
    {
        Agent* a = &s->agents[i];
        pick_spawn(&s->gen, i, &a->x, &a->y);
        a->age = 8 + (int)(brz_hash_u32((uint32_t)i, s->world.seed, 0xA9Eu) % 35u);
        a->household_id = i % s->household_count;

        // distribution: farmers most common
        uint32_t rr = brz_hash_u32((uint32_t)i, s->world.seed, 0xB00Cu) % 100u;
        int vid = default_voc;
        if(rr < 45 && farmer_id>=0) vid = farmer_id;
        else if(rr < 70 && fisher_id>=0) vid = fisher_id;
        else if(rr < 85 && potter_id>=0) vid = potter_id;
        else if(rr < 93 && trader_id>=0) vid = trader_id;
        else if(smith_id>=0) vid = smith_id;
        a->vocation_id = vid;

        a->inv[ITEM_FISH] = 1;
        a->inv[ITEM_GRAIN] = 2;
        a->inv[ITEM_WOOD] = 1;

        a->hunger = 0.2f;
        a->fatigue = 0.1f;
        a->health = 1.0f;
    }

    // choose household parent as oldest member
    for(int h=0; h<s->household_count; h++)
    {
        int parent=-1, bestAge=-1;
        for(int i=0; i<s->agent_count; i++)
        {
            if(s->agents[i].household_id != h) continue;
            if(s->agents[i].age > bestAge)
            {
                bestAge = s->agents[i].age;
                parent = i;
            }
        }
        s->households[h].parent_id = parent;
    }

    cache_init(&s->cache, 8192, cache_max, &s->gen, &s->world);

    s->day = 0;
    s->switch_every_days = 30;
}

void sim_destroy(Sim* s)
{
    cache_destroy(&s->cache);
    free(s->agents);
    free(s->households);
    free(s->settlements);
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

        eat(a);
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
    long inv[ITEM_MAX]= {0};
    int voc_counts[MAX_VOCATIONS]= {0};
    int alive=0;

    for(int i=0; i<s->agent_count; i++)
    {
        const Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;
        alive++;
        for(int it=0; it<ITEM_MAX; it++) inv[it] += a->inv[it];
        if(a->vocation_id >=0 && a->vocation_id < s->voc_table.vocation_count)
        {
            voc_counts[a->vocation_id]++;
        }
    }

    printf("Day %u season=%s alive=%d cache_chunks=%u | fish=%ld grain=%ld wood=%ld clay=%ld cu=%ld tin=%ld bronze=%ld tool=%ld pot=%ld\n",
           s->day, world_season_name(world_season_kind(s->day)), alive, s->cache.live_chunks,
           inv[ITEM_FISH], inv[ITEM_GRAIN], inv[ITEM_WOOD], inv[ITEM_CLAY],
           inv[ITEM_COPPER], inv[ITEM_TIN], inv[ITEM_BRONZE], inv[ITEM_TOOL], inv[ITEM_POT]);

    printf("  vocations:");
    for(int v=0; v<s->voc_table.vocation_count; v++)
    {
        printf(" %s=%d", s->voc_table.vocations[v].name, voc_counts[v]);
    }
    printf("\n");
}

void sim_write_snapshot_json(const Sim* s, const char* path)
{
    FILE* f = fopen(path,"wb");
    if(!f) return;

    long inv[ITEM_MAX]= {0};
    int alive=0;
    int voc_counts[MAX_VOCATIONS]= {0};

    for(int i=0; i<s->agent_count; i++)
    {
        const Agent* a = &s->agents[i];
        if(a->health <= 0.0f) continue;
        alive++;
        for(int it=0; it<ITEM_MAX; it++) inv[it] += a->inv[it];
        if(a->vocation_id >=0 && a->vocation_id < s->voc_table.vocation_count) voc_counts[a->vocation_id]++;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"day\": %u,\n", s->day);
    fprintf(f, "  \"season\": \"%s\",\n", world_season_name(world_season_kind(s->day)));
    fprintf(f, "  \"alive\": %d,\n", alive);
    fprintf(f, "  \"cache_chunks\": %u,\n", s->cache.live_chunks);

    fprintf(f, "  \"inventory\": {\n");
    fprintf(f, "    \"fish\": %ld,\n", inv[ITEM_FISH]);
    fprintf(f, "    \"grain\": %ld,\n", inv[ITEM_GRAIN]);
    fprintf(f, "    \"wood\": %ld,\n", inv[ITEM_WOOD]);
    fprintf(f, "    \"clay\": %ld,\n", inv[ITEM_CLAY]);
    fprintf(f, "    \"copper\": %ld,\n", inv[ITEM_COPPER]);
    fprintf(f, "    \"tin\": %ld,\n", inv[ITEM_TIN]);
    fprintf(f, "    \"bronze\": %ld,\n", inv[ITEM_BRONZE]);
    fprintf(f, "    \"tool\": %ld,\n", inv[ITEM_TOOL]);
    fprintf(f, "    \"pot\": %ld\n", inv[ITEM_POT]);
    fprintf(f, "  },\n");

    fprintf(f, "  \"vocations\": {\n");
    for(int v=0; v<s->voc_table.vocation_count; v++)
    {
        fprintf(f, "    \"%s\": %d%s\n", s->voc_table.vocations[v].name, voc_counts[v],
                (v==s->voc_table.vocation_count-1) ? "" : ",");
    }
    fprintf(f, "  }\n");

    fprintf(f, "}\n");
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
