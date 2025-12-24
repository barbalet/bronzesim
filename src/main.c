#include "brz_parser.h"
#include "brz_sim.h"
#include <stdio.h>
#include <string.h>

static void usage(const char* exe)
{
    printf("Usage: %s <file.bronze>\n", exe);
}

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

    int rc = brz_run(&cfg);
    brz_cfg_free(&cfg);
    return rc;
}
