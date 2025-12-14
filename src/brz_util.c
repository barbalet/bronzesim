/*
 * BRONZESIM — brz_util.c
 *
 * Implementation of low-level utility helpers declared in brz_util.h.
 *
 * These functions are:
 *  - Pure (no side effects other than return values)
 *  - Deterministic
 *  - Safe for use across the entire engine
 *
 * Hash functions are used extensively for:
 *  - Procedural world generation
 *  - Settlement placement
 *  - Vocation distribution
 *  - Random-but-repeatable agent decisions
 */

#include "brz_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void brz_panic(const char* file, int line, const char* expr)
{
    fprintf(stderr, "ASSERT FAIL %s:%d: %s\n", file, line, expr);
    exit(1);
}

uint64_t brz_splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x>>30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x>>27)) * 0x94d049bb133111ebULL;
    return x ^ (x>>31);
}

uint32_t brz_hash_u32(uint32_t a, uint32_t b, uint32_t c)
{
    uint64_t x = ((uint64_t)a<<32) ^ (uint64_t)b ^ ((uint64_t)c<<16);
    return (uint32_t)(brz_splitmix64(x) & 0xFFFFFFFFu);
}

int brz_clamp_i32(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

uint8_t brz_clamp_u8(int v)
{
    if(v < 0) return 0;
    if(v > 255) return 255;
    return (uint8_t)v;
}

bool brz_streq(const char* a, const char* b)
{
    return strcmp(a,b)==0;
}
