/*
 * BRONZESIM — brz_util.h
 *
 * Common utility functions and macros used throughout the project.
 *
 * Includes:
 *  - Assertion handling (BRZ_ASSERT)
 *  - Hashing utilities (SplitMix64-based)
 *  - Clamp helpers for integers and bytes
 *  - String comparison helpers
 *
 * This header intentionally contains no simulation-specific concepts.
 * It exists to avoid duplicated boilerplate and undefined behavior.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BRZ_ASSERT(x) do { if(!(x)) brz_panic(__FILE__, __LINE__, #x); } while(0)

void brz_panic(const char* file, int line, const char* expr);

uint64_t brz_splitmix64(uint64_t x);
uint32_t brz_hash_u32(uint32_t a, uint32_t b, uint32_t c);

int brz_clamp_i32(int v, int lo, int hi);
uint8_t brz_clamp_u8(int v);

bool brz_streq(const char* a, const char* b);
