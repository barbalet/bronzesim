/*
 * BRONZESIM — brz_dsl.c
 *
 * Implements helper logic for working with the BRONZESIM DSL structures
 * after they have been parsed.
 *
 * Responsibilities of this file:
 *  - Validate DSL structures produced by the parser
 *  - Resolve references by name (e.g., rule → task lookup)
 *  - Provide safe fallback behavior for malformed content
 *  - Normalize or patch DSL data so the simulation never crashes
 *
 * Examples of logic found here:
 *  - Finding a task definition by name within a vocation
 *  - Ensuring every vocation has at least one executable task
 *  - Replacing invalid rule references with safe defaults
 *
 * What this file does NOT do:
 *  - It does NOT tokenize or parse text
 *  - It does NOT advance the simulation
 *  - It does NOT implement world or agent logic
 *
 * Why this file exists:
 *  - Keeps the parser simple and permissive
 *  - Prevents hard assertions caused by content authoring mistakes
 *  - Centralizes DSL integrity rules in one place
 *
 * In short:
 *  - brz_parser.c turns text into structs
 *  - brz_dsl.c makes sure those structs are safe and coherent
 *  - brz_sim.c executes them
 */
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


#include "brz_dsl.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>

static char* brz_strdup(const char* s)
{
    if(!s) return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n+1);
    if(!out) return NULL;
    memcpy(out,s,n+1);
    return out;
}

void kind_table_init(KindTable* kt)
{
    if(!kt) return;
    kt->count = 0;
    kt->cap = 0;
    kt->names = NULL;
}

void kind_table_destroy(KindTable* kt)
{
    if(!kt) return;
    for(int i=0;i<kt->count;i++)
        free(kt->names[i]);
    free(kt->names);
    kt->names = NULL;
    kt->count = 0;
    kt->cap = 0;
}

static int kind_table_grow(KindTable* kt, int want_cap)
{
    if(kt->cap >= want_cap) return 0;
    int new_cap = (kt->cap==0)?8:kt->cap;
    while(new_cap < want_cap) new_cap *= 2;
    char** nn = (char**)realloc(kt->names, (size_t)new_cap * sizeof(char*));
    if(!nn) return -1;
    kt->names = nn;
    kt->cap = new_cap;
    return 0;
}

int kind_table_find(const KindTable* kt, const char* name)
{
    if(!kt || !name) return -1;
    for(int i=0;i<kt->count;i++)
    {
        if(strcmp(kt->names[i], name)==0)
            return i;
    }
    return -1;
}

int kind_table_add(KindTable* kt, const char* name)
{
    if(!kt || !name || !name[0]) return -1;
    int existing = kind_table_find(kt, name);
    if(existing >= 0) return existing;

    if(kind_table_grow(kt, kt->count+1) != 0) return -1;
    kt->names[kt->count] = brz_strdup(name);
    if(!kt->names[kt->count]) return -1;
    return kt->count++;
}

const char* kind_table_name(const KindTable* kt, int id)
{
    if(!kt || id < 0 || id >= kt->count) return "";
    return kt->names[id];
}

static const char* normalize_kind_alias(const char* s)
{
    if(!s) return s;
    // resource/item spelling aliases accepted by older DSL files
    if(strcmp(s,"plantfibre")==0) return "plant_fiber";
    if(strcmp(s,"plantfiber")==0) return "plant_fiber";
    if(strcmp(s,"charcoal")==0) return "charcoal";
    return s;
}

int dsl_parse_resource_id(const KindTable* resources, const char* s)
{
    if(!resources || !s) return -1;
    s = normalize_kind_alias(s);
    return kind_table_find(resources, s);
}

int dsl_parse_item_id(const KindTable* items, const char* s)
{
    if(!items || !s) return -1;
    s = normalize_kind_alias(s);
    return kind_table_find(items, s);
}


void voc_table_init(VocationTable* vt)
{
    vt->vocation_cap = MAX_VOCATIONS;
    vt->vocation_count = 0;
    vt->vocations = (VocationDef*)calloc((size_t)vt->vocation_cap, sizeof(VocationDef));
    BRZ_ASSERT(vt->vocations != NULL);
}

static void vocation_init(VocationDef* v, const char* name)
{
    memset(v,0,sizeof(*v));
    strncpy(v->name, name, sizeof(v->name)-1);
    v->task_cap = MAX_TASKS_PER_VOC;
    v->rule_cap = MAX_RULES_PER_VOC;
    v->tasks = (TaskDef*)calloc((size_t)v->task_cap, sizeof(TaskDef));
    v->rules = (RuleDef*)calloc((size_t)v->rule_cap, sizeof(RuleDef));
    BRZ_ASSERT(v->tasks != NULL && v->rules != NULL);
}

static void vocation_destroy(VocationDef* v)
{
    free(v->tasks);
    free(v->rules);
    v->tasks = NULL;
    v->rules = NULL;
    v->task_count = v->rule_count = 0;
    v->task_cap = v->rule_cap = 0;
}

void voc_table_destroy(VocationTable* vt)
{
    if(!vt) return;
    for(int i=0; i<vt->vocation_count; i++)
    {
        vocation_destroy(&vt->vocations[i]);
    }
    free(vt->vocations);
    vt->vocations = NULL;
    vt->vocation_count = vt->vocation_cap = 0;
}

VocationDef* voc_table_add(VocationTable* vt, const char* name)
{
    BRZ_ASSERT(vt->vocation_count < vt->vocation_cap);
    VocationDef* v = &vt->vocations[vt->vocation_count++];
    vocation_init(v, name);
    return v;
}

void vocation_add_task(VocationDef* v, const TaskDef* t)
{
    BRZ_ASSERT(v->task_count < v->task_cap);
    v->tasks[v->task_count++] = *t;
}

void vocation_add_rule(VocationDef* v, const RuleDef* r)
{
    BRZ_ASSERT(v->rule_count < v->rule_cap);
    v->rules[v->rule_count++] = *r;
}





bool dsl_parse_tagbit(const char* s, int* out_tagbit)
{
    if(!s) return false;
    if(strcmp(s,"coast")==0)
    {
        *out_tagbit=TAG_COAST;
        return true;
    }
    if(strcmp(s,"beach")==0)
    {
        *out_tagbit=TAG_BEACH;
        return true;
    }
    if(strcmp(s,"forest")==0)
    {
        *out_tagbit=TAG_FOREST;
        return true;
    }
    if(strcmp(s,"marsh")==0)
    {
        *out_tagbit=TAG_MARSH;
        return true;
    }
    if(strcmp(s,"hill")==0)
    {
        *out_tagbit=TAG_HILL;
        return true;
    }
    if(strcmp(s,"river")==0)
    {
        *out_tagbit=TAG_RIVER;
        return true;
    }
    if(strcmp(s,"field")==0)
    {
        *out_tagbit=TAG_FIELD;
        return true;
    }
    if(strcmp(s,"settlement")==0)
    {
        *out_tagbit=TAG_SETTLE;
        return true;
    }
    return false;
}

int voc_find(const VocationTable* vt, const char* name)
{
    for(int i=0; i<vt->vocation_count; i++)
    {
        if(strcmp(vt->vocations[i].name, name)==0) return i;
    }
    return -1;
}

const VocationDef* voc_get(const VocationTable* vt, int voc_id)
{
    if(voc_id < 0 || voc_id >= vt->vocation_count) return NULL;
    return &vt->vocations[voc_id];
}

TaskDef* voc_task_mut(VocationDef* v, const char* task_name)
{
    for(int i=0; i<v->task_count; i++)
    {
        if(strcmp(v->tasks[i].name, task_name)==0) return &v->tasks[i];
    }
    return NULL;
}

const TaskDef* voc_task(const VocationDef* v, const char* task_name)
{
    for(int i=0; i<v->task_count; i++)
    {
        if(strcmp(v->tasks[i].name, task_name)==0) return &v->tasks[i];
    }
    return NULL;
}

