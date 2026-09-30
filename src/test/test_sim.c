#include "test_common.h"
#include "../brz_parser.h"
#include "../brz_sim.h"
#include "../brz_agent.h"
#include "../brz_settlement.h"
#include "../brz_world.h"

typedef struct {
    char text[2048];
    size_t used;
    int count;
} EventTrace;

static void trace_event(void* context, const BronzeEvent* event)
{
    EventTrace* trace=(EventTrace*)context;
    int written;
    if(!trace || !event || trace->used >= sizeof(trace->text)) return;
    written=snprintf(trace->text+trace->used, sizeof(trace->text)-trace->used,
                     "%d|%u|%d|%s|%.3f|%d|%d\n", (int)event->kind,
                     (unsigned)event->actor_id, event->settlement_id,
                     event->subject ? event->subject : "", event->amount,
                     (int)event->result, event->day);
    if(written > 0){
        size_t added=(size_t)written;
        if(added >= sizeof(trace->text)-trace->used) trace->used=sizeof(trace->text)-1;
        else trace->used+=added;
    }
    trace->count++;
}

static int parse_source(const char* source, ParsedConfig* config, char** path_out)
{
    char* path=brz_test_write_temp("brz_sim_",source);
    if(!path) return 0;
    brz_cfg_init(config);
    if(!brz_parse_file(path,config)){
        brz_cfg_free(config);
        brz_test_unlink(path);
        free(path);
        return 0;
    }
    *path_out=path;
    return 1;
}

static void free_source(ParsedConfig* config, char* path)
{
    brz_cfg_free(config);
    brz_test_unlink(path);
    free(path);
}

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

static void test_fixed_seed_emits_canonical_event_trace(void)
{
    const char* source=
        "kinds { resources { grain } items { token } }\n"
        "world { seed 17 sim_map_w 8 sim_map_h 8 } agents { count 1 } settlements { count 1 } sim { days 2 report_every 0 }\n"
        "actions { action rest 0 0 }\n"
        "vocations { vocation sleeper { task pause { rest } rule always { when true do pause } } }\n";
    ParsedConfig config;
    char* path=NULL;
    EventTrace first={0}, second={0};
    BronzeEventSink first_sink={&first,trace_event};
    BronzeEventSink second_sink={&second,trace_event};

    TEST_ASSERT(parse_source(source,&config,&path));
    TEST_EQ_INT(brz_run_with_events(&config,&first_sink),0);
    TEST_EQ_INT(brz_run_with_events(&config,&second_sink),0);
    TEST_EQ_INT(first.count,4);
    TEST_EQ_INT(second.count,4);
    TEST_STREQ(first.text,second.text);
    TEST_STREQ(first.text,
               "4|0|0|pause|1.000|0|1\n"
               "3|0|0|rest|1.000|0|1\n"
               "4|0|0|pause|1.000|0|2\n"
               "3|0|0|rest|1.000|0|2\n");
    free_source(&config,path);
}

static void test_recipe_consumes_inputs_and_emits_payload(void)
{
    const char* source=
        "kinds { resources { clay } items { pot } }\n"
        "world { seed 3 sim_map_w 8 sim_map_h 8 }\n"
        "actions { action craft 1 2 }\n"
        "recipes { recipe pot { output pot 1 input clay 2 } }\n"
        "vocations { vocation potter { task work { craft pot 1 } rule always { when true do work } } }\n";
    ParsedConfig config;
    char* path=NULL;
    BrzWorld world;
    BrzSettlement* settlements=NULL;
    BronzeWorldPort world_port;
    BronzeSettlementPort settlement_port;
    BronzeActorPort actor_port;
    BrzAgent agent;
    BrzRng rng;
    EventTrace trace={0};
    BronzeEventSink sink={&trace,trace_event};

    memset(&world,0,sizeof(world));
    memset(&world_port,0,sizeof(world_port)); memset(&settlement_port,0,sizeof(settlement_port));
    memset(&agent,0,sizeof(agent));
    TEST_ASSERT(parse_source(source,&config,&path));
    TEST_EQ_INT(brz_world_init(&world,&config,8,8,1),0);
    TEST_EQ_INT(brz_settlements_alloc(&settlements,1,1,1),0);
    settlements[0].pos=(BrzPos){3,3};
    bronze_world_port_init(&world_port,&world,1);
    bronze_settlement_port_init(&settlement_port,settlements,1,&config);
    TEST_ASSERT(world_port.context!=NULL && settlement_port.context!=NULL);
    agent.id=41; agent.voc=(const VocationDef*)brz_vec_cat(&config.vocations,0);
    agent.pos=settlements[0].pos; agent.home_settlement=0;
    agent.res_n=1; agent.item_n=1;
    agent.res_inv=(double*)calloc(1,sizeof(double));
    agent.item_inv=(double*)calloc(1,sizeof(double));
    TEST_ASSERT(agent.res_inv!=NULL && agent.item_inv!=NULL);
    agent.res_inv[0]=2.0;
    brz_rng_seed(&rng,3);
    bronze_actor_port_init(&actor_port,&agent);
    brz_agent_step(&agent,&actor_port,&config,&world_port,&settlement_port,&rng,&sink,9);
    TEST_ASSERT(fabs(agent.res_inv[0]) < 0.000001);
    TEST_ASSERT(fabs(agent.item_inv[0]-1.0) < 0.000001);
    TEST_STREQ(trace.text,
               "4|41|0|work|1.000|0|9\n"
               "1|41|0|pot|1.000|0|9\n");

    free(agent.res_inv); free(agent.item_inv);
    bronze_world_port_destroy(&world_port); bronze_settlement_port_destroy(&settlement_port);
    brz_settlements_free(settlements,1);
    brz_world_free(&world);
    free_source(&config,path);
}

static void test_unavailable_trade_preserves_inventory(void)
{
    const char* source=
        "kinds { resources { wood grain } items { token } }\n"
        "world { seed 5 sim_map_w 8 sim_map_h 8 }\n"
        "actions { action trade 0 2 }\n"
        "resources { resource wood { habitat forest capacity 10 renew 0 nutrition 0 market_target 5 } resource grain { habitat field capacity 10 renew 0 nutrition 0.2 market_target 5 } }\n"
        "vocations { vocation trader { task exchange { trade wood grain } rule always { when true do exchange } } }\n";
    ParsedConfig config;
    char* path=NULL;
    BrzWorld world;
    BrzSettlement* settlements=NULL;
    BronzeWorldPort world_port;
    BronzeSettlementPort settlement_port;
    BronzeActorPort actor_port;
    BrzAgent agent;
    BrzRng rng;
    EventTrace trace={0};
    BronzeEventSink sink={&trace,trace_event};

    memset(&world,0,sizeof(world));
    memset(&world_port,0,sizeof(world_port)); memset(&settlement_port,0,sizeof(settlement_port));
    memset(&agent,0,sizeof(agent));
    TEST_ASSERT(parse_source(source,&config,&path));
    TEST_EQ_INT(brz_world_init(&world,&config,8,8,2),0);
    TEST_EQ_INT(brz_settlements_alloc(&settlements,1,2,1),0);
    settlements[0].pos=(BrzPos){3,3}; settlements[0].res_inv[1]=9.0;
    bronze_world_port_init(&world_port,&world,2);
    bronze_settlement_port_init(&settlement_port,settlements,1,&config);
    TEST_ASSERT(world_port.context!=NULL && settlement_port.context!=NULL);
    agent.id=7; agent.voc=(const VocationDef*)brz_vec_cat(&config.vocations,0);
    agent.pos=settlements[0].pos; agent.home_settlement=0;
    agent.res_n=2; agent.res_inv=(double*)calloc(2,sizeof(double));
    TEST_ASSERT(agent.res_inv!=NULL);
    brz_rng_seed(&rng,5);
    bronze_actor_port_init(&actor_port,&agent);
    brz_agent_step(&agent,&actor_port,&config,&world_port,&settlement_port,&rng,&sink,4);
    TEST_ASSERT(fabs(agent.res_inv[0]) < 0.000001);
    TEST_ASSERT(fabs(agent.res_inv[1]) < 0.000001);
    TEST_ASSERT(fabs(settlements[0].res_inv[0]) < 0.000001);
    TEST_ASSERT(fabs(settlements[0].res_inv[1]-9.0) < 0.000001);
    TEST_STREQ(trace.text,
               "4|7|0|exchange|1.000|0|4\n"
               "2|7|0|wood|0.000|1|4\n");

    free(agent.res_inv);
    bronze_world_port_destroy(&world_port); bronze_settlement_port_destroy(&settlement_port);
    brz_settlements_free(settlements,1);
    brz_world_free(&world);
    free_source(&config,path);
}

static void test_regeneration_is_capped_and_deterministic(void)
{
    const char* source=
        "kinds { resources { grain } items { } }\n"
        "world { seed 11 sim_map_w 8 sim_map_h 8 }\n"
        "resources { resource grain { habitat field capacity 10 renew 0.25 nutrition 0.2 market_target 5 } }\n";
    ParsedConfig config;
    char* path=NULL;
    BrzWorld first, second;
    BrzPos field={0,0};

    memset(&first,0,sizeof(first)); memset(&second,0,sizeof(second));
    TEST_ASSERT(parse_source(source,&config,&path));
    TEST_EQ_INT(brz_world_init(&first,&config,8,8,1),0);
    TEST_EQ_INT(brz_world_init(&second,&config,8,8,1),0);
    first.cap[(size_t)(field.y*first.w+field.x)]=10.0;
    second.cap[(size_t)(field.y*second.w+field.x)]=10.0;
    first.res[(size_t)(field.y*first.w+field.x)]=1.0;
    second.res[(size_t)(field.y*second.w+field.x)]=1.0;
    brz_world_step_regen(&first,1);
    brz_world_step_regen(&second,1);
    TEST_ASSERT(fabs(brz_world_peek(&first,field,1,0)-3.5) < 0.000001);
    TEST_ASSERT(fabs(brz_world_peek(&first,field,1,0)-brz_world_peek(&second,field,1,0)) < 0.000001);
    for(int i=0;i<10;i++) brz_world_step_regen(&first,1);
    TEST_ASSERT(fabs(brz_world_peek(&first,field,1,0)-10.0) < 0.000001);

    brz_world_free(&first); brz_world_free(&second);
    free_source(&config,path);
}

static void test_bronze_event_projects_to_shared_runtime_contract(void)
{
    BronzeEvent bronze;
    ScenarioEvent expected, actual;
    memset(&bronze,0,sizeof(bronze));
    bronze.kind=BRZ_EVENT_TRADED;
    bronze.actor_id=7;
    bronze.settlement_id=2;
    bronze.subject="water_provision";
    bronze.amount=1.0;
    bronze.result=BRZ_RESULT_COMPLETED;
    bronze_event_to_scenario(&bronze,60,1.0,&actual);
    scenario_event_init(&expected,60,7,SCENARIO_ID_NONE,2,"water_provision",
                        1.0,1.0,SCENARIO_RESULT_COMPLETED);
    TEST_ASSERT(scenario_event_equivalent(&actual,&expected));
}

static void test_bronze_adapter_runs_completed_and_deferred_service_vectors(void)
{
    BrzAgent recipient;
    BrzSettlement settlement;
    BronzeScenarioClockView clock;
    BronzeScenarioPlaceView place_view;
    ScenarioWorldPort world;
    ScenarioActorPort actor;
    ScenarioPlacePort place;
    ScenarioServiceFixture completed, deferred, expected;
    memset(&recipient,0,sizeof(recipient)); memset(&settlement,0,sizeof(settlement));
    memset(&world,0,sizeof(world)); memset(&actor,0,sizeof(actor)); memset(&place,0,sizeof(place));
    recipient.id=8; recipient.pos=(BrzPos){12,9};
    settlement.pos=(BrzPos){12,9};
    bronze_scenario_clock_view_init(&clock,60,&world);
    bronze_scenario_actor_view_init(&recipient,&actor);
    bronze_scenario_place_view_init(&settlement,2,&place_view,&place);
    TEST_ASSERT(scenario_service_fixture_run(&world,&actor,&place,7,"water_provision",1.0,
                                             SCENARIO_RESULT_COMPLETED,&completed));
    TEST_ASSERT(scenario_service_fixture_run(&world,&actor,&place,7,"water_provision",1.0,
                                             SCENARIO_RESULT_DEFERRED,&deferred));
    scenario_event_init(&expected.events[0],60,8,SCENARIO_ID_NONE,2,"travel",1.0,1.0,
                        SCENARIO_RESULT_COMPLETED);
    scenario_event_init(&expected.events[1],61,7,8,2,"water_provision",1.0,1.0,
                        SCENARIO_RESULT_COMPLETED);
    expected.recipient_position=(ScenarioPosition){12,9};
    expected.recipient_place_id=2; expected.recipient_arrived=1; expected.delivered_amount=1.0;
    TEST_ASSERT(scenario_service_fixture_equivalent(&completed,&expected));
    TEST_EQ_INT(deferred.events[1].result,SCENARIO_RESULT_DEFERRED);
    TEST_ASSERT(deferred.delivered_amount==0.0);
}

void test_sim_run(void)
{
    test_runtime_emits_selection_event();
    test_fixed_seed_emits_canonical_event_trace();
    test_recipe_consumes_inputs_and_emits_payload();
    test_unavailable_trade_preserves_inventory();
    test_regeneration_is_capped_and_deterministic();
    test_bronze_event_projects_to_shared_runtime_contract();
    test_bronze_adapter_runs_completed_and_deferred_service_vectors();
}
