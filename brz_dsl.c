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

#include "brz_dsl.h"
#include "brz_util.h"
#include <stdlib.h>
#include <string.h>

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



bool dsl_parse_resource(const char* s, ResourceKind* out)
{
    if(!s) return false;
    if(strcmp(s,"fish")==0)
    {
        *out=RES_FISH;
        return true;
    }
    if(strcmp(s,"grain")==0)
    {
        *out=RES_GRAIN;
        return true;
    }
    if(strcmp(s,"wood")==0)
    {
        *out=RES_WOOD;
        return true;
    }
    if(strcmp(s,"clay")==0)
    {
        *out=RES_CLAY;
        return true;
    }
    if(strcmp(s,"copper")==0)
    {
        *out=RES_COPPER;
        return true;
    }
    if(strcmp(s,"tin")==0)
    {
        *out=RES_TIN;
        return true;
    }

    if(strcmp(s,"fire")==0)
    {
        *out=RES_FIRE;
        return true;
    }
    if(strcmp(s,"plant_fiber")==0 || strcmp(s,"plantfibre")==0 || strcmp(s,"plant_fibre")==0)
    {
        *out=RES_PLANT_FIBER;
        return true;
    }
    if(strcmp(s,"cattle")==0)
    {
        *out=RES_CATTLE;
        return true;
    }
    if(strcmp(s,"sheep")==0)
    {
        *out=RES_SHEEP;
        return true;
    }
    if(strcmp(s,"pig")==0)
    {
        *out=RES_PIG;
        return true;
    }
    if(strcmp(s,"charcoal")==0)
    {
        *out=RES_CHARCOAL;
        return true;
    }
    if(strcmp(s,"religion")==0)
    {
        *out=RES_RELIGION;
        return true;
    }
    if(strcmp(s,"nationalism")==0)
    {
        *out=RES_NATIONALISM;
        return true;
    }
    return false;
}

bool dsl_parse_item(const char* s, ItemKind* out)
{
    if(!s) return false;
    if(strcmp(s,"fish")==0)
    {
        *out=ITEM_FISH;
        return true;
    }
    if(strcmp(s,"grain")==0)
    {
        *out=ITEM_GRAIN;
        return true;
    }
    if(strcmp(s,"wood")==0)
    {
        *out=ITEM_WOOD;
        return true;
    }
    if(strcmp(s,"clay")==0)
    {
        *out=ITEM_CLAY;
        return true;
    }
    if(strcmp(s,"copper")==0)
    {
        *out=ITEM_COPPER;
        return true;
    }
    if(strcmp(s,"tin")==0)
    {
        *out=ITEM_TIN;
        return true;
    }
    if(strcmp(s,"bronze")==0)
    {
        *out=ITEM_BRONZE;
        return true;
    }
    if(strcmp(s,"tool")==0)
    {
        *out=ITEM_TOOL;
        return true;
    }
    if(strcmp(s,"pot")==0)
    {
        *out=ITEM_POT;
        return true;
    }
    return false;
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

