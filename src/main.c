/*
 * BRONZESIM — main.c
 *
 * This is the program entry point.
 *
 * Responsibilities:
 *  - Load and parse the BRONZESIM DSL file (example.bronze)
 *  - Convert parsed DSL data into runtime simulation structures
 *  - Initialize the world, chunk cache, agents, settlements, and vocations
 *  - Run the main simulation loop for N days
 *  - Periodically print simulation status summaries
 *  - Cleanly shut down and free memory
 *
 * main.c deliberately contains NO simulation logic.
 * It wires together the parser, world, cache, and simulation engine.
 *
 * This separation ensures that:
 *  - The DSL can evolve independently
 *  - The simulation engine remains reusable
 *  - The executable stays small and deterministic
 */

#include "brz_parser.h"
#include "brz_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* exe)
{
    printf("Usage: %s [config.bronze]\n", exe);
}

int main(int argc, char** argv)
{
    const char* path = (argc >= 2) ? argv[1] : "example.bronze";
    if(argc >= 2 && (strcmp(argv[1],"-h")==0 || strcmp(argv[1],"--help")==0))
    {
        print_usage(argv[0]);
        return 0;
    }

    ParsedConfig pcfg;
    if(!brz_parse_file(path, &pcfg))
    {
        fprintf(stderr, "Failed to parse %s\n", path);
        return 1;
    }

    if(pcfg.voc_table.vocation_count == 0)
    {
        fprintf(stderr, "Error: example must include vocations { ... } with at least 1 vocation.\n");
        return 2;
    }

    WorldSpec spec;
    spec.seed = pcfg.seed;
    spec.settlement_count = pcfg.settlement_count;

    spec.res_model.renew_per_day[RES_FISH]   = pcfg.fish_renew;
    spec.res_model.renew_per_day[RES_GRAIN]  = pcfg.grain_renew;
    spec.res_model.renew_per_day[RES_WOOD]   = pcfg.wood_renew;
    spec.res_model.renew_per_day[RES_CLAY]   = pcfg.clay_renew;
    spec.res_model.renew_per_day[RES_COPPER] = pcfg.copper_renew;
    spec.res_model.renew_per_day[RES_TIN]    = pcfg.tin_renew;

    spec.res_model.renew_per_day[RES_FIRE]        = pcfg.fire_renew;
    spec.res_model.renew_per_day[RES_PLANT_FIBER] = pcfg.plant_fiber_renew;
    spec.res_model.renew_per_day[RES_CATTLE]      = pcfg.cattle_renew;
    spec.res_model.renew_per_day[RES_SHEEP]       = pcfg.sheep_renew;
    spec.res_model.renew_per_day[RES_PIG]         = pcfg.pig_renew;
    spec.res_model.renew_per_day[RES_CHARCOAL]    = pcfg.charcoal_renew;
    spec.res_model.renew_per_day[RES_RELIGION]    = pcfg.religion_renew;
    spec.res_model.renew_per_day[RES_NATIONALISM] = pcfg.nationalism_renew;
    printf("BRONZESIM world: 30mi x 30mi at 3ft => %d x %d cells (~%.2fB)\n",
           WORLD_CELLS_X, WORLD_CELLS_Y, (double)WORLD_CELLS_X*(double)WORLD_CELLS_Y/1e9);
    printf("Config: seed=%u days=%d agents=%d settlements=%d cache_max=%u snapshot_every=%d map_every=%d\n",
           pcfg.seed, pcfg.days, pcfg.agent_count, pcfg.settlement_count, pcfg.cache_max,
           pcfg.snapshot_every_days, pcfg.map_every_days);

    printf("Loaded vocations: %d\n", pcfg.voc_table.vocation_count);
    for(int i=0; i<pcfg.voc_table.vocation_count; i++)
    {
        printf("  %s (tasks=%d rules=%d)\n",
               pcfg.voc_table.vocations[i].name,
               pcfg.voc_table.vocations[i].task_count,
               pcfg.voc_table.vocations[i].rule_count);
    }

    Sim sim;
    sim_init(&sim, &spec, pcfg.cache_max, pcfg.agent_count, &pcfg.voc_table);

    for(int d=0; d<pcfg.days; d++)
    {
        sim_step(&sim);

        if((sim.day % 10u)==0) sim_report(&sim);

        if(pcfg.snapshot_every_days > 0 && (sim.day % (uint32_t)pcfg.snapshot_every_days)==0)
        {
            char fn[128];
            snprintf(fn,sizeof(fn),"snapshot_day%05u.json", sim.day);
            sim_write_snapshot_json(&sim, fn);
        }

        if(pcfg.map_every_days > 0 && (sim.day % (uint32_t)pcfg.map_every_days)==0)
        {
            char fn[128];
            snprintf(fn,sizeof(fn),"map_day%05u.txt", sim.day);
            sim_dump_ascii_map(&sim, fn, 80, 40);
        }
    }

    sim_report(&sim);
    sim_destroy(&sim);
    voc_table_destroy(&pcfg.voc_table);
    return 0;
}
