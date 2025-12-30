#include "brz_parser.h"
#include "brz_sim.h"
#include "brz_util.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void usage(const char* exe)
{
    printf("Usage: %s [file.bronze]\n", exe);
    printf("Outputs:\n");
    printf("  snapshot_dayNNNNN.json and map_dayNNNNN.txt are controlled by sim { snapshot_every, map_every }\n");
}

static int find_param_int(const ParsedConfig* cfg, const char* key, int defv)
{
    if(!cfg) return defv;
    size_t n = cfg->params.len;
    for(size_t i=0;i<n;i++)
    {
        const ParamDef* p = (const ParamDef*)brz_vec_at((BrzVec*)&cfg->params, i);
        if(p && p->key && brz_streq(p->key, key) && !p->has_svalue)
            return (int)floor(p->value + 0.5);
    }
    return defv;
}

#ifndef __APPLE__

int main(int argc, char** argv)
{
    const char* path = (argc >= 2) ? argv[1] : "example.bronze";
    if(argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    {
        usage(argv[0]);
        return 0;
    }

    ParsedConfig cfg;
    brz_cfg_init(&cfg);

    if(!brz_parse_file(path, &cfg))
    {
        brz_cfg_free(&cfg);
        return 1;
    }

    /* legacy-style banner (restored) */
    int days = find_param_int(&cfg, "sim_days", find_param_int(&cfg, "cycles", 60));
    int snapshot_every = find_param_int(&cfg, "sim_snapshot_every", 0);
    int map_every = find_param_int(&cfg, "sim_map_every", 0);
    int cache_max = find_param_int(&cfg, "sim_cache_max", 0);

    printf("Config: seed=%u days=%d agents=%d settlements=%d cache_max=%d snapshot_every=%d map_every=%d\n",
           cfg.seed, days, cfg.agent_count, cfg.settlement_count, cache_max, snapshot_every, map_every);

    printf("Loaded vocations: %zu\n", cfg.vocations.len);
    for(size_t i=0; i<cfg.vocations.len; i++)
    {
        const VocationDef* v = (const VocationDef*)brz_vec_at((BrzVec*)&cfg.vocations, i);
        if(!v) continue;
        printf("  %s (tasks=%zu rules=%zu)\n",
               v->name ? v->name : "(null)",
               v->tasks.len,
               v->rules.len);
    }

    int rc = brz_run(&cfg);
    brz_cfg_free(&cfg);
    return rc;
}

#endif
