#ifndef BRZ_KINDS_H
#define BRZ_KINDS_H

#include <stdbool.h>

/*
 * Dynamic kinds
 *
 * Resource kinds and Item kinds are defined by the DSL (example.bronze),
 * not hard-coded in C enums.
 *
 * Kinds are stored as an ordered list of names. The numeric id is the
 * index into that list (0..count-1). IDs are stable within a single run
 * and the order is controlled by the DSL file.
 */
typedef struct
{
    int count;
    int cap;
    char** names;   // owned strings
} KindTable;

void kind_table_init(KindTable* kt);
void kind_table_destroy(KindTable* kt);

// Add (or find existing) kind name; returns id (>=0) or -1 on OOM
int kind_table_add(KindTable* kt, const char* name);

// Find kind id by name; returns id (>=0) or -1 if missing
int kind_table_find(const KindTable* kt, const char* name);

// Convenience: safe name lookup; returns "" if out of range
const char* kind_table_name(const KindTable* kt, int id);

#endif
