#include "brz_dsl.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>


/*
DSL_GRAMMAR_BEGIN
# NOTE: This block is the single source of truth for the BRONZESIM DSL grammar.
# It is extracted and injected into DSL_MANUAL.md automatically (make -C src docs).
#
# Conventions:
#   - 'literal' denotes a keyword or symbol token.
#   - identifier / number / string are lexical tokens.
#   - { X } means repetition (zero or more).
#   - [ X ] means optional.
#
# This grammar describes the *surface syntax*. The engine imposes additional semantic rules.

program             := { top_level_block } EOF ;

top_level_block     := world_block
                    | kinds_block
                    | resources_block
                    | items_block
                    | vocations_block
                    | compat_block ;

# ----- Blocks -----

world_block          := 'world' block_open { world_stmt } block_close ;
kinds_block          := 'kinds' block_open { kind_def } block_close ;
resources_block      := 'resources' block_open { resource_def } block_close ;
items_block          := 'items' block_open { item_def } block_close ;

vocations_block      := 'vocations' block_open { vocation_def } block_close ;
vocation_def         := 'vocation' identifier block_open { vocation_member } block_close ;
vocation_member      := task_def | rule_def ;

task_def             := 'task' identifier block_open { task_stmt } block_close ;
rule_def             := 'rule' identifier block_open { rule_stmt } block_close ;

# ----- World statements -----
# The world block is intentionally permissive: keys are identifiers.
# Values can be number, string, or identifier.

world_stmt           := identifier value ';' ;
value                := number | string | identifier ;

# ----- Registry definitions -----

kind_def             := identifier ';' ;
resource_def         := identifier ':' identifier ';' ;
item_def             := identifier ':' identifier ';' ;

# ----- Rule / task language -----

rule_stmt            := when_block
                    | do_stmt
                    | chance_block
                    | ';' ;

task_stmt            := action_stmt
                    | do_stmt
                    | when_block
                    | chance_block
                    | ';' ;

# Common structured statements
when_block           := 'when' condition block_open { task_stmt } block_close ;
chance_block         := 'chance' number block_open { task_stmt } block_close ;
do_stmt              := 'do' identifier ';' ;

# Conditions are intentionally simple in the core grammar.
# The engine may accept additional operators in future revisions.

condition            := identifier cond_op cond_rhs ;
cond_op              := '<' | '<=' | '>' | '>=' | '==' | '!=' ;
cond_rhs             := number | identifier ;

# Actions are a small, engine-defined set of verbs.
# Extend the verb set in the engine and keep the grammar here in sync.

action_stmt          := action_verb identifier number ';' ;
action_verb          := 'gather' | 'craft' | 'trade' ;

# ----- Lexical helpers -----

block_open           := '{' ;
block_close          := '}' ;

# ----- Compatibility blocks -----
# Older examples may use these blocks. They are accepted for backwards compatibility
# and may be mapped internally onto the newer registries.

compat_block         := 'sim' block_open { compat_stmt } block_close
                     | 'agents' block_open { compat_stmt } block_close ;

compat_stmt          := identifier { identifier | number | string | ':' | ';' | '{' | '}' } ;
DSL_GRAMMAR_END
*/

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
