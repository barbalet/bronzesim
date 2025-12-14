/*
 * BRONZESIM — brz_cache.c
 *
 * Implements the chunk cache and LRU eviction system.
 *
 * Responsibilities:
 *  - Allocate and free chunk memory
 *  - Generate chunks using the world generator
 *  - Track chunk usage order
 *  - Perform daily regeneration on all loaded chunks
 *
 * Memory safety notes:
 *  - All chunk allocations are heap-based
 *  - No pointer aliasing across eviction
 *  - No dangling references retained by agents
 *
 * This is one of the most performance-critical files.
 */

#include "brz_cache.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>

static uint32_t key_hash(int32_t cx, int32_t cy)
{
    return brz_hash_u32((uint32_t)cx, (uint32_t)cy, 0xA5A5A5A5u);
}

static void lru_remove(ChunkCache* cc, Chunk* ch)
{
    if(ch->lru_prev) ch->lru_prev->lru_next = ch->lru_next;
    if(ch->lru_next) ch->lru_next->lru_prev = ch->lru_prev;
    if(cc->lru_head == ch) cc->lru_head = ch->lru_next;
    if(cc->lru_tail == ch) cc->lru_tail = ch->lru_prev;
    ch->lru_prev = ch->lru_next = NULL;
}

static void lru_push_front(ChunkCache* cc, Chunk* ch)
{
    ch->lru_prev = NULL;
    ch->lru_next = cc->lru_head;
    if(cc->lru_head) cc->lru_head->lru_prev = ch;
    cc->lru_head = ch;
    if(!cc->lru_tail) cc->lru_tail = ch;
}

static Chunk* hash_find(ChunkCache* cc, int32_t cx, int32_t cy)
{
    uint32_t b = key_hash(cx,cy) % cc->bucket_count;
    Chunk* it = cc->buckets[b];
    while(it)
    {
        if(it->cx == cx && it->cy == cy) return it;
        it = it->hash_next;
    }
    return NULL;
}

static void hash_insert(ChunkCache* cc, Chunk* ch)
{
    uint32_t b = key_hash(ch->cx,ch->cy) % cc->bucket_count;
    ch->hash_next = cc->buckets[b];
    cc->buckets[b] = ch;
}

static void hash_remove(ChunkCache* cc, Chunk* ch)
{
    uint32_t b = key_hash(ch->cx,ch->cy) % cc->bucket_count;
    Chunk** pp = &cc->buckets[b];
    while(*pp)
    {
        if(*pp == ch)
        {
            *pp = ch->hash_next;
            ch->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

static void generate_chunk(ChunkCache* cc, Chunk* ch)
{
    WorldGen* g = cc->gen;
    int32_t base_x = ch->cx * CHUNK_SIZE;
    int32_t base_y = ch->cy * CHUNK_SIZE;

    for(int iy=0; iy<CHUNK_SIZE; iy++)
    {
        for(int ix=0; ix<CHUNK_SIZE; ix++)
        {
            int idx = iy*CHUNK_SIZE + ix;
            int32_t wx = base_x + ix;
            int32_t wy = base_y + iy;

            uint8_t tags = world_cell_tags(g, wx, wy, cc->spec->settlement_count);
            ch->terrain[idx] = tags;

            for(int r=0; r<RES_MAX; r++)
            {
                ch->res[r][idx] = world_cell_res0(g, wx, wy, (ResourceKind)r, tags);
            }
        }
    }
}

static void evict_one(ChunkCache* cc)
{
    Chunk* victim = cc->lru_tail;
    if(!victim) return;
    lru_remove(cc, victim);
    hash_remove(cc, victim);
    free(victim);
    cc->live_chunks--;
}

void cache_init(ChunkCache* c, uint32_t bucket_count, uint32_t max_chunks, WorldGen* gen, WorldSpec* spec)
{
    memset(c, 0, sizeof(*c));
    c->bucket_count = bucket_count;
    c->buckets = (Chunk**)calloc(bucket_count, sizeof(Chunk*));
    BRZ_ASSERT(c->buckets != NULL);
    c->max_chunks = (max_chunks < 16) ? 16 : max_chunks;
    c->gen = gen;
    c->spec = spec;
}

void cache_destroy(ChunkCache* c)
{
    Chunk* it = c->lru_head;
    while(it)
    {
        Chunk* nxt = it->lru_next;
        free(it);
        it = nxt;
    }
    free(c->buckets);
    memset(c, 0, sizeof(*c));
}

Chunk* cache_get_chunk(ChunkCache* cc, int32_t cx, int32_t cy)
{
    Chunk* ch = hash_find(cc, cx, cy);
    if(ch)
    {
        lru_remove(cc, ch);
        lru_push_front(cc, ch);
        return ch;
    }

    while(cc->live_chunks >= cc->max_chunks)
    {
        evict_one(cc);
    }

    ch = (Chunk*)calloc(1, sizeof(Chunk));
    BRZ_ASSERT(ch != NULL);
    ch->cx = cx;
    ch->cy = cy;

    generate_chunk(cc, ch);
    hash_insert(cc, ch);
    lru_push_front(cc, ch);
    cc->live_chunks++;
    return ch;
}

Chunk* cache_get_cell(ChunkCache* cc, int32_t x, int32_t y, int* out_idx)
{
    x = brz_clamp_i32(x, 0, (int32_t)WORLD_CELLS_X-1);
    y = brz_clamp_i32(y, 0, (int32_t)WORLD_CELLS_Y-1);

    int32_t cx = x / CHUNK_SIZE;
    int32_t cy = y / CHUNK_SIZE;
    int32_t lx = x % CHUNK_SIZE;
    int32_t ly = y % CHUNK_SIZE;
    *out_idx = (int)(ly*CHUNK_SIZE + lx);
    return cache_get_chunk(cc, cx, cy);
}

void cache_regen_loaded(ChunkCache* cc, SeasonKind season)
{
    float fishMul  = (season==SEASON_WINTER) ? 0.70f : 1.0f;
    float grainMul = (season==SEASON_WINTER) ? 0.30f : (season==SEASON_SUMMER || season==SEASON_AUTUMN ? 1.0f : 0.70f);

    for(Chunk* ch = cc->lru_head; ch; ch = ch->lru_next)
    {
        for(int i=0; i<CHUNK_CELLS; i++)
        {
            uint8_t tags = ch->terrain[i];


            if(tags & TAG_SETTLE)
            {
                int fi = (int)ch->res[RES_FIRE][i] + (int)(cc->spec->res_model.renew_per_day[RES_FIRE] * 255.0f);
                int re = (int)ch->res[RES_RELIGION][i] + (int)(cc->spec->res_model.renew_per_day[RES_RELIGION] * 255.0f);
                int na = (int)ch->res[RES_NATIONALISM][i] + (int)(cc->spec->res_model.renew_per_day[RES_NATIONALISM] * 255.0f);
                ch->res[RES_FIRE][i] = brz_clamp_u8(fi);
                ch->res[RES_RELIGION][i] = brz_clamp_u8(re);
                ch->res[RES_NATIONALISM][i] = brz_clamp_u8(na);
            }
            if(tags & TAG_COAST)
            {
                int v = (int)ch->res[RES_FISH][i] + (int)(cc->spec->res_model.renew_per_day[RES_FISH] * fishMul * 255.0f);
                ch->res[RES_FISH][i] = brz_clamp_u8(v);
            }
            if(tags & TAG_FIELD)
            {
                int v = (int)ch->res[RES_GRAIN][i] + (int)(cc->spec->res_model.renew_per_day[RES_GRAIN] * grainMul * 255.0f);
                ch->res[RES_GRAIN][i] = brz_clamp_u8(v);
            }
            if(tags & TAG_FIELD)
            {
                int pf = (int)ch->res[RES_PLANT_FIBER][i] + (int)(cc->spec->res_model.renew_per_day[RES_PLANT_FIBER] * 255.0f);
                int ca = (int)ch->res[RES_CATTLE][i] + (int)(cc->spec->res_model.renew_per_day[RES_CATTLE] * 255.0f);
                int sh = (int)ch->res[RES_SHEEP][i] + (int)(cc->spec->res_model.renew_per_day[RES_SHEEP] * 255.0f);
                int pg = (int)ch->res[RES_PIG][i] + (int)(cc->spec->res_model.renew_per_day[RES_PIG] * 255.0f);
                ch->res[RES_PLANT_FIBER][i] = brz_clamp_u8(pf);
                ch->res[RES_CATTLE][i] = brz_clamp_u8(ca);
                ch->res[RES_SHEEP][i] = brz_clamp_u8(sh);
                ch->res[RES_PIG][i] = brz_clamp_u8(pg);
            }
            if(tags & TAG_FOREST)
            {
                int v = (int)ch->res[RES_WOOD][i] + (int)(cc->spec->res_model.renew_per_day[RES_WOOD] * 255.0f);
                ch->res[RES_WOOD][i] = brz_clamp_u8(v);
            }
            if(tags & TAG_FOREST)
            {
                int chv = (int)ch->res[RES_CHARCOAL][i] + (int)(cc->spec->res_model.renew_per_day[RES_CHARCOAL] * 255.0f);
                ch->res[RES_CHARCOAL][i] = brz_clamp_u8(chv);
            }
            if((tags & TAG_RIVER) || (tags & TAG_MARSH))
            {
                int v = (int)ch->res[RES_CLAY][i] + (int)(cc->spec->res_model.renew_per_day[RES_CLAY] * 255.0f);
                ch->res[RES_CLAY][i] = brz_clamp_u8(v);
            }
            if(tags & TAG_HILL)
            {
                int cu = (int)ch->res[RES_COPPER][i] + (int)(cc->spec->res_model.renew_per_day[RES_COPPER] * 255.0f);
                int tn = (int)ch->res[RES_TIN][i]    + (int)(cc->spec->res_model.renew_per_day[RES_TIN] * 255.0f);
                ch->res[RES_COPPER][i] = brz_clamp_u8(cu);
                ch->res[RES_TIN][i]    = brz_clamp_u8(tn);
            }
        }
    }
}
