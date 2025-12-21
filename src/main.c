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

        // World + kinds are now fully defined by the DSL and moved into the Sim during sim_init().

Sim sim;
    sim_init(&sim, &pcfg);

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
