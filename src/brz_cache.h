/*
 * BRONZESIM — brz_cache.h
 *
 * Defines the chunk cache system.
 *
 * The chunk cache:
 *  - Materializes small regions (chunks) of the world on demand
 *  - Stores terrain + resource data
 *  - Evicts least-recently-used chunks automatically
 *
 * This enables:
 *  - Massive world sizes
 *  - Predictable memory usage
 *  - Efficient agent movement and resource access
 */

#pragma once
#include <stdint.h>
#include "brz_world.h"

#define CHUNK_SIZE 64
#define CHUNK_CELLS (CHUNK_SIZE*CHUNK_SIZE)

typedef struct Chunk
{
    int32_t cx, cy;
    uint8_t terrain[CHUNK_CELLS];
    uint8_t* res; // [spec->resources.count * CHUNK_CELLS] row-major: res[rk*CHUNK_CELLS + i]

    struct Chunk* hash_next;
    struct Chunk* lru_prev;
    struct Chunk* lru_next;
} Chunk;

typedef struct
{
    Chunk** buckets;
    uint32_t bucket_count;

    Chunk* lru_head;
    Chunk* lru_tail;

    uint32_t live_chunks;
    uint32_t max_chunks;

    WorldGen* gen;
    WorldSpec* spec;
} ChunkCache;

void cache_init(ChunkCache* c, uint32_t bucket_count, uint32_t max_chunks, WorldGen* gen, WorldSpec* spec);
void cache_destroy(ChunkCache* c);

Chunk* cache_get_chunk(ChunkCache* c, int32_t cx, int32_t cy);
Chunk* cache_get_cell(ChunkCache* c, int32_t x, int32_t y, int* out_idx);

// Daily regeneration for all loaded chunks
void cache_regen_loaded(ChunkCache* c, SeasonKind season);
