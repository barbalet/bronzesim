/****************************************************************

 shared.c

 =============================================================

 Copyright 1996-2025 Tom Barbalet. All rights reserved.

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or
 sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

 ****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Vendored BRONZESIM core.
   shared.c lives in bronzevis-mac/bronzevis-mac/gui/, so ../bronzesim/src is adjacent. */
#include "../bronzesim/src/brz_parser.h"
#include "../bronzesim/src/brz_dsl.h"
#include "../bronzesim/src/brz_kinds.h"
#include "../bronzesim/src/brz_world.h"
#include "../bronzesim/src/brz_settlement.h"
#include "../bronzesim/src/brz_agent.h"
#include "../bronzesim/src/brz_util.h"

/* Fixed output surface (requested): 1024 x 800 RGBA(ish)
   Swift uses byteOrder32Big | noneSkipFirst -> memory layout is X R G B. */
#define FB_W 1024
#define FB_H 800
#define SCREEN_SIZE (FB_W * FB_H * 4)

static unsigned char outputBuffer[ SCREEN_SIZE ];

/* ---------------- config helpers ---------------- */

static const ParamDef* cfg_find_param(const ParsedConfig* cfg, const char* key)
{
    for(size_t i=0;i<cfg->params.len;i++){
        const ParamDef* p = (const ParamDef*)brz_vec_cat(&cfg->params, i);
        if(p->key && brz_streq(p->key, key)) return p;
    }
    return NULL;
}

static int cfg_get_int(const ParsedConfig* cfg, const char* key, int defv)
{
    const ParamDef* p = cfg_find_param(cfg, key);
    if(!p) return defv;
    if(p->has_svalue) return defv;
    return (int)(p->value);
}

/* ---------------- realtime sim state ---------------- */

typedef struct {
    int ready;

    ParsedConfig cfg;
    int cfg_loaded;

    BrzWorld world;
    int world_inited;

    BrzSettlement* setts;
    int sett_n;

    BrzAgent* agents;
    int agent_n;

    BrzRng rng;
    size_t res_n;
    size_t item_n;

    int map_w;
    int map_h;

    unsigned long day;

    /* pacing: Swift passes milliseconds as ticks */
    unsigned long last_ms;
    unsigned long accum_ms;
} BrzRealtime;

static BrzRealtime rt;

/* ---------------- framebuffer helpers ---------------- */

static void rt_clear_frame(unsigned char r, unsigned char g, unsigned char b)
{
    for(int y=0; y<FB_H; y++){
        unsigned char* row = &outputBuffer[(size_t)y * (size_t)FB_W * 4u];
        for(int x=0; x<FB_W; x++){
            row[x*4 + 0] = 0; /* X */
            row[x*4 + 1] = r;
            row[x*4 + 2] = g;
            row[x*4 + 3] = b;
        }
    }
}

static inline void set_px(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    if((unsigned)x >= (unsigned)FB_W) return;
    if((unsigned)y >= (unsigned)FB_H) return;
    size_t i = ((size_t)y * (size_t)FB_W + (size_t)x) * 4u;
    outputBuffer[i + 0] = 0;
    outputBuffer[i + 1] = r;
    outputBuffer[i + 2] = g;
    outputBuffer[i + 3] = b;
}

static void fill_rect(int x0, int y0, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    if(w <= 0 || h <= 0) return;
    int x1 = x0 + w;
    int y1 = y0 + h;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > FB_W) x1 = FB_W;
    if(y1 > FB_H) y1 = FB_H;
    for(int y=y0; y<y1; y++){
        unsigned char* row = &outputBuffer[(size_t)y * (size_t)FB_W * 4u];
        for(int x=x0; x<x1; x++){
            row[x*4 + 0] = 0;
            row[x*4 + 1] = r;
            row[x*4 + 2] = g;
            row[x*4 + 3] = b;
        }
    }
}

static void glyph_color(uint16_t tags, unsigned char* r, unsigned char* g, unsigned char* b)
{
    /* Priority ordering: coast > fire > mines > clay > forest > field > default */

    if(tags & BRZ_TAG_COAST)   {
        /* Deep coastal blue / sea water */
        *r =  40; *g =  90; *b = 160;
        return;
    }

    if(tags & BRZ_TAG_FIRE)    {
        /* Bright orange / flame */
        *r = 220; *g = 120; *b =  40;
        return;
    }

    if(tags & BRZ_TAG_MINE_CU) {
        /* Cool gray-blue / copper ore rock */
        *r = 120; *g = 120; *b = 140;
        return;
    }

    if(tags & BRZ_TAG_MINE_SN) {
        /* Slightly darker bluish gray / tin ore rock */
        *r = 110; *g = 110; *b = 130;
        return;
    }

    if(tags & BRZ_TAG_CLAYPIT) {
        /* Reddish brown / exposed clay soil */
        *r = 160; *g =  80; *b =  70;
        return;
    }

    if(tags & BRZ_TAG_FOREST)  {
        /* Deep green / woodland */
        *r =  40; *g = 110; *b =  60;
        return;
    }

    if(tags & BRZ_TAG_FIELD)   {
        /* Yellow-green / grassland or crops */
        *r = 150; *g = 160; *b =  70;
        return;
    }

    /* Neutral dark gray / unknown or barren terrain */
    *r = 60; *g = 60; *b = 60;
}

static unsigned hash_u32(const char* s)
{
    /* tiny stable hash for vocation->color */
    uint32_t h = 2166136261u;
    if(!s) return h;
    while(*s){
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void vocation_color(const VocationDef* voc, unsigned char* r, unsigned char* g, unsigned char* b)
{
    unsigned h = hash_u32(voc ? voc->name : "");
    /* Keep them bright-ish */
    *r = (unsigned char)(80 + (h & 0x7F));
    *g = (unsigned char)(80 + ((h >> 8) & 0x7F));
    *b = (unsigned char)(80 + ((h >> 16) & 0x7F));
}

/* ---------------- sim lifecycle ---------------- */

static void rt_shutdown(void)
{
    if(rt.agents){ brz_agents_free(rt.agents, rt.agent_n); rt.agents = NULL; }
    if(rt.setts){ brz_settlements_free(rt.setts, rt.sett_n); rt.setts = NULL; }
    if(rt.world_inited){ brz_world_free(&rt.world); rt.world_inited = 0; }
    if(rt.cfg_loaded){ brz_cfg_free(&rt.cfg); rt.cfg_loaded = 0; }

    memset(&rt, 0, sizeof(rt));
}

int brz_shared_load_config(const char* path)
{
    if(!path || !path[0]){
        fprintf(stderr, "brz_shared_load_config: missing path\n");
        return 1;
    }

    rt_shutdown();
    brz_cfg_init(&rt.cfg);

    if(!brz_parse_file(path, &rt.cfg)){
        fprintf(stderr, "brz_shared_load_config: parse failed: %s\n", path);
        brz_cfg_free(&rt.cfg);
        return 2;
    }
    rt.cfg_loaded = 1;

    rt.res_n  = kind_table_count(&rt.cfg.resource_kinds);
    rt.item_n = kind_table_count(&rt.cfg.item_kinds);

    /* For realtime rendering, defaults target a ~160x125 tile map (fits 1024x800 at 6px tiles). */
    rt.map_w = cfg_get_int(&rt.cfg, "sim_map_w", 160);
    rt.map_h = cfg_get_int(&rt.cfg, "sim_map_h", 125);
    rt.map_w = (rt.map_w < 8) ? 8 : rt.map_w;
    rt.map_h = (rt.map_h < 8) ? 8 : rt.map_h;
    rt.map_w = (rt.map_w > 512) ? 512 : rt.map_w;
    rt.map_h = (rt.map_h > 512) ? 512 : rt.map_h;

    rt.agent_n = (rt.cfg.agent_count > 0) ? rt.cfg.agent_count : (int)rt.cfg.vocations.len;
    if(rt.agent_n <= 0) rt.agent_n = 32;

    rt.sett_n = (rt.cfg.settlement_count > 0) ? rt.cfg.settlement_count : 1;
    if(rt.sett_n <= 0) rt.sett_n = 1;

    if(brz_world_init(&rt.world, &rt.cfg, rt.map_w, rt.map_h, rt.res_n) != 0){
        fprintf(stderr, "brz_shared_load_config: world init failed\n");
        rt_shutdown();
        return 3;
    }
    rt.world_inited = 1;

    if(brz_settlements_alloc(&rt.setts, rt.sett_n, rt.res_n, rt.item_n) != 0){
        fprintf(stderr, "brz_shared_load_config: settlement alloc failed\n");
        rt_shutdown();
        return 4;
    }

    brz_settlements_place(rt.setts, rt.sett_n, rt.map_w, rt.map_h, rt.cfg.seed ? rt.cfg.seed : 0xC0FFEEu);
    brz_world_stamp_fields_around_settlements(&rt.world, rt.setts, rt.sett_n, 8);

    if(brz_agents_alloc_and_spawn(&rt.agents, rt.agent_n, &rt.cfg,
                                  rt.setts, rt.sett_n,
                                  rt.res_n, rt.item_n,
                                  rt.cfg.seed ? rt.cfg.seed : 0xC0FFEEu) != 0){
        fprintf(stderr, "brz_shared_load_config: agent alloc failed\n");
        rt_shutdown();
        return 5;
    }

    /* population count */
    for(int si=0; si<rt.sett_n; si++) rt.setts[si].population = 0;
    for(int ai=0; ai<rt.agent_n; ai++){
        int h = rt.agents[ai].home_settlement;
        if(h>=0 && h<rt.sett_n) rt.setts[h].population++;
    }

    brz_rng_seed(&rt.rng, rt.cfg.seed ? rt.cfg.seed : 0xC0FFEEu);

    rt.ready = 1;
    rt.day = 1;
    rt.last_ms = 0;
    rt.accum_ms = 0;

    return 0;
}

/* ---------------- rendering ---------------- */

static void rt_render(void)
{
    if(!rt.ready){
        rt_clear_frame(25, 25, 25);
        return;
    }

    /* Map-to-screen transform */
    int tile_px = 1;
    if(rt.map_w > 0 && rt.map_h > 0){
        int tx = FB_W / rt.map_w;
        int ty = FB_H / rt.map_h;
        tile_px = (tx < ty) ? tx : ty;
        if(tile_px < 1) tile_px = 1;
    }

    int map_px_w = rt.map_w * tile_px;
    int map_px_h = rt.map_h * tile_px;
    int off_x = (FB_W - map_px_w) / 2;
    int off_y = (FB_H - map_px_h) / 2;

    /* Background outside map */
    rt_clear_frame(18, 18, 18);

    /* Geography */
    for(int y=0; y<rt.map_h; y++){
        for(int x=0; x<rt.map_w; x++){
            uint16_t tags = rt.world.tags ? rt.world.tags[y*rt.map_w + x] : 0;
            unsigned char r,g,b;
            glyph_color(tags, &r, &g, &b);
            fill_rect(off_x + x*tile_px, off_y + y*tile_px, tile_px, tile_px, r, g, b);
        }
    }

    /* Settlements */
    for(int i=0; i<rt.sett_n; i++){
        int sx = off_x + rt.setts[i].pos.x * tile_px;
        int sy = off_y + rt.setts[i].pos.y * tile_px;
        fill_rect(sx-1, sy-1, tile_px+2, tile_px+2, 0, 0, 0);
        fill_rect(sx,   sy,   tile_px,   tile_px,   240, 240, 240);
    }

    /* Agents */
    for(int i=0; i<rt.agent_n; i++){
        const BrzAgent* a = &rt.agents[i];
        int ax = off_x + a->pos.x * tile_px + tile_px/2;
        int ay = off_y + a->pos.y * tile_px + tile_px/2;
        unsigned char r,g,b;
        vocation_color(a->voc, &r, &g, &b);
        for(int dy=-1; dy<=1; dy++)
            for(int dx=-1; dx<=1; dx++)
                set_px(ax+dx, ay+dy, r, g, b);
    }

    /* Simple HUD strip: day count as a bright bar that grows */
    {
        int w = (int)(rt.day % (unsigned)FB_W);
        if(w < 8) w = 8;
        fill_rect(0, 0, w, 3, 255, 255, 255);
    }
}

/* ---------------- exported API (Swift calls these) ---------------- */

void brz_shared_cycle(unsigned long ticks)
{
    /* ticks is milliseconds from Swift */
    const unsigned long STEP_MS = 250;   /* 4 sim-days per second */
    const unsigned long MAX_CATCHUP_STEPS = 8;

    if(!rt.ready){
        rt_render();
        return;
    }

    if(rt.last_ms == 0){
        rt.last_ms = ticks;
    }

    unsigned long dt = (ticks >= rt.last_ms) ? (ticks - rt.last_ms) : 0;
    rt.last_ms = ticks;
    rt.accum_ms += dt;

    unsigned long steps = 0;
    while(rt.accum_ms >= STEP_MS && steps < MAX_CATCHUP_STEPS){
        /* One sim "day" */
        brz_world_step_regen(&rt.world, rt.res_n);
        brz_settlements_begin_day(rt.setts, rt.sett_n);
        for(int i=0;i<rt.agent_n;i++)
            brz_agent_step(&rt.agents[i], &rt.cfg, &rt.world, rt.setts, rt.sett_n, &rt.rng);

        rt.day++;
        rt.accum_ms -= STEP_MS;
        steps++;
    }

    rt_render();
}

void brz_shared_init(unsigned long random)
{
    (void)random;
    memset(&rt, 0, sizeof(rt));
    rt_clear_frame(25, 25, 25);
}

void brz_shared_close(void)
{
    rt_shutdown();
}

unsigned char * brz_shared_draw(long dim_x, long dim_y)
{
    (void)dim_x;
    (void)dim_y;
    return outputBuffer;
}
