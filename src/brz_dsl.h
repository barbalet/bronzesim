/*
 * BRONZESIM — brz_dsl.h
 *
 * DSL = Domain-Specific Language.
 *
 * This header defines the in-memory representation of the BRONZESIM DSL.
 * The DSL is the human-authored text format used in example.bronze.
 *
 * Responsibilities of this file:
 *  - Define C structs that represent parsed DSL concepts:
 *      - Vocations (occupations)
 *      - Tasks
 *      - Rules
 *      - Primitive operations (move_to, gather, craft, trade, rest, roam)
 *  - Provide enums and constants that map DSL keywords to runtime meanings
 *  - Establish hard limits (max tasks, rules, operations) for memory safety
 *
 * What this file does NOT do:
 *  - It does NOT parse text
 *  - It does NOT execute tasks
 *  - It does NOT contain simulation logic
 *
 * Think of this file as the “schema” or “ABI” of the DSL:
 * it defines the shape of the data that flows from text → parser → simulator.
 *
 * Design goals:
 *  - Fixed-size, heap-safe structures (no unbounded allocation)
 *  - Fast lookup during simulation
 *  - Clear separation between content (DSL) and behavior (engine)
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "brz_kinds.h"
#include "brz_world.h"

// Inventory items (includes crafted goods)
typedef enum { CMP_ANY=0, CMP_GT, CMP_LT, CMP_GE, CMP_LE } CmpKind;

typedef struct
{
    // Conditions are ANDed together.
    bool has_hunger;
    float hunger_threshold;  // hunger > threshold

    bool has_fatigue;
    float fatigue_threshold; // fatigue < threshold

    SeasonKind season_eq;    // == season; SEASON_ANY means ignore

    // Up to 4 inventory clauses: inv <item> <cmp> <value>
    int inv_count;
    int inv_item[4]; // item kind id (index into DSL-defined item kinds)
    CmpKind inv_cmp[4];
    int inv_value[4];

    bool has_prob;
    float prob;              // 0..1
} Condition;

typedef enum
{
    OP_MOVE_TO=0,      // arg_j = tag bit (TAG_*)
    OP_GATHER,         // arg_j = resource kind id, arg_i = amount
    OP_CRAFT,          // arg_j = item kind id, arg_i = amount
    OP_TRADE,          // no args
    OP_REST,           // no args
    OP_ROAM            // arg_i = steps
} OpKind;

typedef struct
{
    OpKind kind;
    int arg_i;
    int arg_j;
} OpDef;

#define MAX_OPS_PER_TASK  16
#define MAX_TASKS_PER_VOC 64
#define MAX_RULES_PER_VOC 64
#define MAX_VOCATIONS     128

typedef struct
{
    char name[64];
    int op_count;
    OpDef ops[MAX_OPS_PER_TASK];
} TaskDef;

typedef struct
{
    char name[64];
    Condition cond;
    char task_name[64];     // resolved to task_index during parsing
    int task_index;         // -1 if unresolved
    int weight;
} RuleDef;

typedef struct
{
    char name[64];

    int task_count;
    int task_cap;
    TaskDef* tasks;

    int rule_count;
    int rule_cap;
    RuleDef* rules;
} VocationDef;

typedef struct
{
    VocationDef* vocations;
    int vocation_count;
    int vocation_cap;
} VocationTable;


// Allocation helpers (heap-backed so configs can be large without stack overflow)
void voc_table_init(VocationTable* vt);
void voc_table_destroy(VocationTable* vt);
VocationDef* voc_table_add(VocationTable* vt, const char* name);
void vocation_add_task(VocationDef* v, const TaskDef* t);
void vocation_add_rule(VocationDef* v, const RuleDef* r);

// Helpers for mapping DSL identifiers to ids/bits
int dsl_parse_resource_id(const KindTable* resources, const char* s);
int dsl_parse_item_id(const KindTable* items, const char* s);
bool dsl_parse_tagbit(const char* s, int* out_tagbit);

// Find vocation/task by name
int voc_find(const VocationTable* vt, const char* name);
const VocationDef* voc_get(const VocationTable* vt, int voc_id);
TaskDef* voc_task_mut(VocationDef* v, const char* task_name);
const TaskDef* voc_task(const VocationDef* v, const char* task_name);
