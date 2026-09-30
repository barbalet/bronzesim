
#include "test_common.h"
#include "../brz_parser.h"
#include "../brz_dsl.h"
#include "../brz_kinds.h"

static bool parse_from_string(const char* s, ParsedConfig* cfg)
{
    char* path = brz_test_write_temp("brz_parse_", s);
    if(!path) return false;
    bool ok = brz_parse_file(path, cfg);
    brz_test_unlink(path);
    free(path);
    return ok;
}

static const ParamDef* find_param(const ParsedConfig* cfg, const char* key)
{
    for(size_t i=0;i<cfg->params.len;i++)
    {
        const ParamDef* p = (const ParamDef*)brz_vec_cat(&cfg->params, i);
        if(p && p->key && strcmp(p->key, key)==0) return p;
    }
    return NULL;
}

static void test_parse_minimal_success(void)
{
    const char* src =
        "kinds { resources { fish grain } items { fish bronze } }\n"
        "world { seed 1337 years 30 }\n"
        "agents { count 10 }\n"
        "settlements { count 2 }\n"
        "resources { fish_renew 0.08 }\n"
        "items { bronze item }\n"
        "vocations {\n"
        "  vocation fisher {\n"
        "    task gather_food { gather fish }\n"
        "    rule r1 { when true do gather_food weight 3 }\n"
        "  }\n"
        "}\n";

    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src, &cfg));

    TEST_EQ_INT(cfg.seed, 1337u);
    TEST_EQ_INT(cfg.years, 30);
    TEST_EQ_INT(cfg.agent_count, 10);
    TEST_EQ_INT(cfg.settlement_count, 2);

    TEST_EQ_SIZE(kind_table_count(&cfg.resource_kinds), 2);
    TEST_EQ_SIZE(kind_table_count(&cfg.item_kinds), 2); /* fish, bronze (from kinds + items block adds/dups) */
    TEST_EQ_INT(kind_table_find(&cfg.resource_kinds, "fish"), 0);
    TEST_EQ_INT(kind_table_find(&cfg.item_kinds, "bronze"), 1);

    TEST_EQ_SIZE(cfg.vocations.len, 1);
    VocationDef* v = (VocationDef*)brz_vec_at(&cfg.vocations, 0);
    TEST_ASSERT(v && v->name);
    TEST_STREQ(v->name, "fisher");
    TEST_EQ_SIZE(v->tasks.len, 1);
    TEST_EQ_SIZE(v->rules.len, 1);

    TaskDef* t = brz_voc_find_task(v, "gather_food");
    TEST_ASSERT(t != NULL);
    TEST_EQ_SIZE(t->stmts.len, 1);

    RuleDef* r = (RuleDef*)brz_vec_at(&v->rules, 0);
    TEST_ASSERT(r != NULL);
    TEST_STREQ(r->name, "r1");
    TEST_STREQ(r->do_task, "gather_food");
    TEST_EQ_INT(r->weight, 3);

    const ParamDef* p_seed = find_param(&cfg, "world_seed");
    TEST_ASSERT(p_seed != NULL);
    TEST_ASSERT(!p_seed->has_svalue);
    TEST_EQ_INT((int)p_seed->value, 1337);

    const ParamDef* p_renew = find_param(&cfg, "fish_renew");
    TEST_ASSERT(p_renew != NULL);
    TEST_ASSERT(!p_renew->has_svalue);

    brz_cfg_free(&cfg);
}

static void test_parse_resources_mapping_form(void)
{
    const char* src =
        "kinds { resources { fish } items { fish } }\n"
        "resources { fish resource }\n";

    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src, &cfg));
    TEST_EQ_SIZE(kind_table_count(&cfg.resource_kinds), 1);
    TEST_EQ_INT(kind_table_find(&cfg.resource_kinds, "fish"), 0);
    brz_cfg_free(&cfg);
}

static void test_parse_items_mapping_form_adds_kind(void)
{
    const char* src =
        "kinds { resources { } items { } }\n"
        "items { pot item tool item }\n";

    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src, &cfg));
    TEST_EQ_SIZE(kind_table_count(&cfg.item_kinds), 2);
    TEST_EQ_INT(kind_table_find(&cfg.item_kinds, "pot"), 0);
    TEST_EQ_INT(kind_table_find(&cfg.item_kinds, "tool"), 1);
    brz_cfg_free(&cfg);
}

static void test_parse_world_agents_settlements_defaults(void)
{
    /* if missing, keep DSL defaults from brz_cfg_init */
    const char* src =
        "kinds { resources { fish } items { fish } }\n";

    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    uint32_t def_seed = cfg.seed;
    int def_years = cfg.years;

    TEST_ASSERT(parse_from_string(src, &cfg));
    TEST_EQ_INT(cfg.seed, def_seed);
    TEST_EQ_INT(cfg.years, def_years);
    TEST_EQ_INT(cfg.agent_count, 0);
    TEST_EQ_INT(cfg.settlement_count, 0);
    brz_cfg_free(&cfg);
}

static void test_parse_task_stmt_variants(void)
{
    const char* src =
        "kinds { resources { fish } items { fish } }\n"
        "vocations {\n"
        "  vocation t1 {\n"
        "    task a {\n"
        "      chance 50 { gather fish }\n"
        "      when hungry { rest }\n"
        "      move_to coast\n"
        "    }\n"
        "    rule rr { when true do a weight 1 }\n"
        "  }\n"
        "}\n";
    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src, &cfg));
    VocationDef* v = (VocationDef*)brz_vec_at(&cfg.vocations, 0);
    TEST_ASSERT(v != NULL);
    TaskDef* t = brz_voc_find_task(v, "a");
    TEST_ASSERT(t != NULL);
    TEST_EQ_SIZE(t->stmts.len, 3);
    brz_cfg_free(&cfg);
}

static void test_parse_data_defined_economy(void)
{
    const char* src =
        "kinds { resources { grain clay } items { pot } }\n"
        "resources { resource grain { habitat field capacity 120 renew 0.1 nutrition 0.2 market_target 80 } resource clay { habitat claypit capacity 20 renew 0 nutrition 0 market_target 10 } }\n"
        "recipes { recipe pot { output pot 1 input clay 2 } }\n"
        "actions { action gather 1 2 action craft 1 2 }\n"
        "settlement_policy { deposit_threshold 3 rest_recovery 0.05 }\n"
        "vocations { vocation potter { task work {\n"
        "gather clay 2\ncraft pot 1\n"
        "} rule r { when true do work } } }\n";
    ParsedConfig cfg; brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src,&cfg));
    const ResourceDef* grain=brz_resource_find(&cfg,"grain");
    const RecipeDef* pot=brz_recipe_find(&cfg,"pot");
    TEST_ASSERT(grain && pot);
    TEST_STREQ(grain->habitat,"field");
    TEST_EQ_INT((int)grain->capacity,120);
    TEST_EQ_INT(grain->kind_id,0);
    TEST_ASSERT(grain->terrain_query!=0);
    TEST_EQ_SIZE(pot->inputs.len,1);
    TEST_STREQ(((const RecipeInputDef*)brz_vec_cat(&pot->inputs,0))->kind,"clay");
    TEST_EQ_INT((int)cfg.settlement_policy.deposit_threshold,3);
    brz_cfg_free(&cfg);
}

static void test_compile_rejects_invalid_runtime_operands(void)
{
    const char* bad_habitat=
        "kinds { resources { grain } items { } }\n"
        "resources { resource grain { habitat marsh capacity 1 renew 0 nutrition 0 market_target 1 } }\n";
    const char* bad_action=
        "kinds { resources { grain } items { } }\n"
        "actions { action gather 1 1 }\n"
        "vocations { vocation farmer { task work { gather clay } rule r { when true do work } } }\n";
    ParsedConfig cfg; brz_cfg_init(&cfg);
    TEST_ASSERT(!parse_from_string(bad_habitat,&cfg));
    brz_cfg_free(&cfg); brz_cfg_init(&cfg);
    TEST_ASSERT(!parse_from_string(bad_action,&cfg));
    brz_cfg_free(&cfg);
}

static void test_parse_mosul_scenario_profile(void)
{
    const char* src =
        "kinds { resources { water } items { } }\n"
        "scenario { version 1 profile mosul }\n"
        "map_refs { map mosul_navigation_v1 }\n"
        "places { place al_hadidi_home { kind home map_ref mosul_navigation_v1 capacity 8 } place al_furat_water { kind water_point map_ref mosul_navigation_v1 capacity 10 } }\n"
        "roles { role civilian { min_age 0 } role water_technician { workplace al_furat_water min_age 18 } }\n"
        "needs { need water { priority 10 } need work { priority 4 } }\n"
        "services { service water_provision { provider water_technician recipient civilian place al_furat_water input water output water price 2 } }\n"
        "disruptions { disruption water_closure { affects water_provision } }\n";
    ParsedConfig cfg; brz_cfg_init(&cfg);
    TEST_ASSERT(parse_from_string(src,&cfg));
    TEST_EQ_INT(cfg.language_version,1);
    TEST_STREQ(cfg.scenario_profile,"mosul");
    TEST_EQ_SIZE(cfg.map_refs.len,1);
    TEST_EQ_SIZE(cfg.places.len,2);
    TEST_EQ_SIZE(cfg.roles.len,2);
    TEST_EQ_SIZE(cfg.needs.len,2);
    TEST_EQ_SIZE(cfg.services.len,1);
    TEST_EQ_SIZE(cfg.disruptions.len,1);
    TEST_STREQ(((const ServiceDef*)brz_vec_cat(&cfg.services,0))->provider_role,"water_technician");
    brz_cfg_free(&cfg);
}

static void test_parse_errors_return_false(void)
{
    /* unknown top-level */
    {
        const char* src = "nope { a b }\n";
        ParsedConfig cfg; brz_cfg_init(&cfg);
        TEST_ASSERT(!parse_from_string(src, &cfg));
        brz_cfg_free(&cfg);
    }
    /* missing brace */
    {
        const char* src = "kinds { resources { fish } items { fish }\n"; /* no closing } */
        ParsedConfig cfg; brz_cfg_init(&cfg);
        TEST_ASSERT(!parse_from_string(src, &cfg));
        brz_cfg_free(&cfg);
    }
    /* vocations expects 'vocation' keyword */
    {
        const char* src = "kinds { resources { fish } items { fish } }\n"
                          "vocations { job farmer { } }\n";
        ParsedConfig cfg; brz_cfg_init(&cfg);
        TEST_ASSERT(!parse_from_string(src, &cfg));
        brz_cfg_free(&cfg);
    }
    /* rule missing name */
    {
        const char* src = "kinds { resources { fish } items { fish } }\n"
                          "vocations { vocation v { task t { rest } rule { when true do t } } }\n";
        ParsedConfig cfg; brz_cfg_init(&cfg);
        TEST_ASSERT(!parse_from_string(src, &cfg));
        brz_cfg_free(&cfg);
    }
}

void test_parser_run(void)
{
    test_parse_minimal_success();
    test_parse_resources_mapping_form();
    test_parse_items_mapping_form_adds_kind();
    test_parse_world_agents_settlements_defaults();
    test_parse_task_stmt_variants();
    test_parse_data_defined_economy();
    test_compile_rejects_invalid_runtime_operands();
    test_parse_mosul_scenario_profile();
    test_parse_errors_return_false();
}
