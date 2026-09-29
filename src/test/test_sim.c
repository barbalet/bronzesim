#include "test_common.h"
#include "../brz_parser.h"
#include "../brz_sim.h"

typedef struct { int selections; int day; } EventCount;

static void count_event(void* context, const BronzeEvent* event)
{
    EventCount* count=(EventCount*)context;
    if(event->kind==BRZ_EVENT_OCCUPATION_SELECTED){ count->selections++; count->day=event->day; }
}

static void test_runtime_emits_selection_event(void)
{
    const char* source=
        "kinds { resources { grain } items { } }\n"
        "world { seed 7 sim_map_w 8 sim_map_h 8 } agents { count 1 } settlements { count 1 } sim { days 1 report_every 0 }\n"
        "resources { resource grain { habitat field capacity 10 renew 0 nutrition 0.2 market_target 10 } }\n"
        "actions { action gather 1 2 }\n"
        "vocations { vocation farmer { task work { gather grain 1 } rule work_now { when true do work } } }\n";
    char* path=brz_test_write_temp("brz_event_",source);
    ParsedConfig config; EventCount count={0,0}; BronzeEventSink sink;
    brz_cfg_init(&config);
    TEST_ASSERT(path!=NULL);
    TEST_ASSERT(brz_parse_file(path,&config));
    sink.context=&count; sink.emit=count_event;
    TEST_EQ_INT(brz_run_with_events(&config,&sink),0);
    TEST_EQ_INT(count.selections,1);
    TEST_EQ_INT(count.day,1);
    brz_cfg_free(&config); brz_test_unlink(path); free(path);
}

void test_sim_run(void)
{
    test_runtime_emits_selection_event();
}
