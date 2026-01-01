
#include "test_common.h"
#include "../brz_dsl.h"
#include "../brz_util.h"

static void test_cfg_init_defaults(void)
{
    ParsedConfig cfg;
    memset(&cfg, 0xA5, sizeof(cfg));
    brz_cfg_init(&cfg);

    TEST_ASSERT(cfg.seed != 0);
    TEST_ASSERT(cfg.years > 0);
    TEST_EQ_INT(cfg.agent_count, 0);
    TEST_EQ_INT(cfg.settlement_count, 0);

    TEST_EQ_SIZE(cfg.params.len, 0);
    TEST_EQ_SIZE(cfg.vocations.len, 0);
    TEST_EQ_SIZE(kind_table_count(&cfg.resource_kinds), 0);
    TEST_EQ_SIZE(kind_table_count(&cfg.item_kinds), 0);

    brz_cfg_free(&cfg);
}

static void test_voc_find_task(void)
{
    VocationDef v;
    memset(&v, 0, sizeof(v));
    v.name = brz_strdup("testvoc");
    brz_vec_init(&v.tasks, sizeof(TaskDef));
    brz_vec_init(&v.rules, sizeof(RuleDef));

    TEST_ASSERT(brz_voc_find_task(&v, "missing") == NULL);
    TEST_ASSERT(brz_voc_find_task(NULL, "x") == NULL);
    TEST_ASSERT(brz_voc_find_task(&v, NULL) == NULL);

    TaskDef t1; memset(&t1, 0, sizeof(t1));
    t1.name = brz_strdup("alpha");
    brz_vec_init(&t1.stmts, sizeof(StmtDef));
    TEST_ASSERT(brz_vec_push(&v.tasks, &t1));

    TaskDef t2; memset(&t2, 0, sizeof(t2));
    t2.name = brz_strdup("beta");
    brz_vec_init(&t2.stmts, sizeof(StmtDef));
    TEST_ASSERT(brz_vec_push(&v.tasks, &t2));

    TaskDef* f1 = brz_voc_find_task(&v, "alpha");
    TaskDef* f2 = brz_voc_find_task(&v, "beta");
    TaskDef* f3 = brz_voc_find_task(&v, "gamma");

    TEST_ASSERT(f1 != NULL);
    TEST_ASSERT(f2 != NULL);
    TEST_ASSERT(f3 == NULL);
    TEST_STREQ(f1->name, "alpha");
    TEST_STREQ(f2->name, "beta");

    /* cleanup using cfg_free path to exercise voc_free/task_free */
    ParsedConfig cfg;
    brz_cfg_init(&cfg);
    TEST_ASSERT(brz_vec_push(&cfg.vocations, &v));
    brz_cfg_free(&cfg);
}

static void test_cfg_free_clears_state(void)
{
    ParsedConfig cfg;
    brz_cfg_init(&cfg);

    /* add some params */
    ParamDef p; memset(&p,0,sizeof(p));
    p.key = brz_strdup("x");
    p.value = 3.14;
    p.has_svalue = false;
    TEST_ASSERT(brz_vec_push(&cfg.params, &p));

    /* add some kinds */
    TEST_EQ_INT(kind_table_add(&cfg.resource_kinds, "fish"), 0);
    TEST_EQ_INT(kind_table_add(&cfg.item_kinds, "pot"), 0);

    /* add vocation with empty blocks */
    VocationDef v; memset(&v,0,sizeof(v));
    v.name = brz_strdup("v");
    brz_vec_init(&v.tasks, sizeof(TaskDef));
    brz_vec_init(&v.rules, sizeof(RuleDef));
    TEST_ASSERT(brz_vec_push(&cfg.vocations, &v));

    brz_cfg_free(&cfg);

    TEST_EQ_INT(cfg.seed, 0);
    TEST_EQ_INT(cfg.years, 0);
    TEST_EQ_SIZE(cfg.params.len, 0);
    TEST_EQ_SIZE(cfg.vocations.len, 0);
    TEST_EQ_SIZE(kind_table_count(&cfg.resource_kinds), 0);
    TEST_EQ_SIZE(kind_table_count(&cfg.item_kinds), 0);
}

void test_dsl_run(void)
{
    test_cfg_init_defaults();
    test_voc_find_task();
    test_cfg_free_clears_state();
}
