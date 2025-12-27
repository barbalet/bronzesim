#include "brz_dsl.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>

static void op_free(OpDef* op)
{
    if(!op) return;
    free(op->op);
    free(op->a0);
    free(op->a1);
    free(op->a2);
    memset(op, 0, sizeof(*op));
}

static void stmt_free(StmtDef* st);

static void stmt_vec_free(BrzVec* v)
{
    if(!v) return;
    for(size_t i=0;i<v->len;i++)
    {
        StmtDef* s = (StmtDef*)brz_vec_at(v, i);
        stmt_free(s);
    }
    brz_vec_destroy(v);
}

static void stmt_free(StmtDef* st)
{
    if(!st) return;
    switch(st->kind)
    {
        case ST_OP:
            op_free(&st->as.op);
            break;
        case ST_CHANCE:
            stmt_vec_free(&st->as.chance.body);
            break;
        case ST_WHEN:
            free(st->as.when_stmt.when_expr);
            stmt_vec_free(&st->as.when_stmt.body);
            break;
        default:
            break;
    }
    memset(st, 0, sizeof(*st));
}

static void task_free(TaskDef* t)
{
    if(!t) return;
    free(t->name);
    for(size_t i=0;i<t->stmts.len;i++)
    {
        StmtDef* s = (StmtDef*)brz_vec_at(&t->stmts, i);
        stmt_free(s);
    }
    brz_vec_destroy(&t->stmts);
    memset(t, 0, sizeof(*t));
}

static void rule_free(RuleDef* r)
{
    if(!r) return;
    free(r->name);
    free(r->when_expr);
    free(r->do_task);
    memset(r, 0, sizeof(*r));
}

static void voc_free(VocationDef* v)
{
    if(!v) return;
    free(v->name);

    for(size_t i=0;i<v->tasks.len;i++)
    {
        TaskDef* t = (TaskDef*)brz_vec_at(&v->tasks, i);
        task_free(t);
    }
    brz_vec_destroy(&v->tasks);

    for(size_t i=0;i<v->rules.len;i++)
    {
        RuleDef* r = (RuleDef*)brz_vec_at(&v->rules, i);
        rule_free(r);
    }
    brz_vec_destroy(&v->rules);

    memset(v, 0, sizeof(*v));
}

static void param_free(ParamDef* p)
{
    if(!p) return;
    free(p->key);
    free(p->svalue);
    memset(p, 0, sizeof(*p));
}

void brz_cfg_init(ParsedConfig* cfg)
{
    if(!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = 0xC0FFEEu;
    cfg->years = 60;
    cfg->agent_count = 0;
    cfg->settlement_count = 0;
    kind_table_init(&cfg->resource_kinds);
    kind_table_init(&cfg->item_kinds);
    brz_vec_init(&cfg->params, sizeof(ParamDef));
    brz_vec_init(&cfg->vocations, sizeof(VocationDef));
}

void brz_cfg_free(ParsedConfig* cfg)
{
    if(!cfg) return;

    for(size_t i=0;i<cfg->params.len;i++)
    {
        ParamDef* p = (ParamDef*)brz_vec_at(&cfg->params, i);
        param_free(p);
    }
    brz_vec_destroy(&cfg->params);

    for(size_t i=0;i<cfg->vocations.len;i++)
    {
        VocationDef* v = (VocationDef*)brz_vec_at(&cfg->vocations, i);
        voc_free(v);
    }
    brz_vec_destroy(&cfg->vocations);

    kind_table_destroy(&cfg->resource_kinds);
    kind_table_destroy(&cfg->item_kinds);

    memset(cfg, 0, sizeof(*cfg));
}

TaskDef* brz_voc_find_task(VocationDef* voc, const char* name)
{
    if(!voc || !name) return NULL;
    for(size_t i=0;i<voc->tasks.len;i++)
    {
        TaskDef* t = (TaskDef*)brz_vec_at(&voc->tasks, i);
        if(t->name && brz_streq(t->name, name)) return t;
    }
    return NULL;
}
