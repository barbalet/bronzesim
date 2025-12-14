# BRONZESIM Technical Specification

This is the **formal** specification for the BRONZESIM engine as implemented in the C sources in this repository.
It is written to help maintainers and contributors keep the codebase coherent as the DSL grows.

## 1. Scope and goals

BRONZESIM models a small society (agents grouped into settlements) living on a procedurally generated island world.
The **DSL** defines occupations (vocations), tasks (ordered operations), and rules (conditions → task selection).

The engine prioritizes:

- **Determinism:** identical inputs yield identical outcomes (including world generation).
- **Data-driven behavior:** vocation logic should live in the DSL.
- **Bounded memory:** the world is too large to hold fully; only active chunks are cached.
- **Portability:** ISO C99, no external libraries.

Non-goals include real-time rendering, networking, or embedding a general scripting language.

## 2. Top-level pipeline

1. **Parse DSL** (`brz_parse_file` in `brz_parser.c`) into a `ParsedConfig`.
2. **Initialize world generation** (`worldgen_init` in `brz_world.c`) using the parsed seed.
3. **Initialize chunk cache** (`cache_init` in `brz_cache.c`) with an LRU policy.
4. **Initialize simulation** (`sim_init` in `brz_sim.c`): create settlements, agents, inventories, and initial state.
5. **Run for N days** (`sim_step` loop in `main.c`), periodically writing snapshots / ASCII maps.

## 3. Determinism requirements

Determinism is required across:

- **World generation:** `WorldGen` uses a fixed seed and hash-based sampling.
- **Chunk loading:** chunk contents must be a pure function of (seed, chunk coords, day/season).
- **Simulation step:** task selection and resource updates must not depend on timing, OS, or floating nondeterminism.

The engine uses integer clamping and bounded floats to keep values stable.

## 4. World model constraints

- The world is addressed in **cells** (3 ft × 3 ft).
- It is logically huge; therefore, only **chunks** are stored at runtime.
- Each cell can have:
  - a **tag bitfield** (coast, beach, forest, marsh, hill, river, field, settlement)
  - per-resource availability (sampled via deterministic functions)

## 5. Cache model constraints

- Chunk cache has a fixed maximum number of chunks (`cache_max`).
- Retrieval uses (x,y) → (chunk coords + local index).
- Cache uses LRU eviction to ensure bounded memory and predictable cost.

## 6. DSL contract with the runtime

The DSL produces these runtime objects:

- `VocationDef` — a named occupation
- `TaskDef` — a named task containing an ordered list of operations
- `RuleDef` — a named rule with:
  - a condition (hunger/fatigue/season/inventory/probability)
  - a target task name
  - a selection weight

The runtime requires:

- Every referenced task name in rules must exist within the same vocation.
- Operations must use known resources/items/tags.

(If the DSL is malformed, the code asserts; see error policy below.)

## 7. Error policy

BRONZESIM is designed for *author-authored* DSL, not hostile input.

- Syntax and semantic errors are treated as **fatal** (`BRZ_ASSERT`).
- The engine prefers **early failure** over continuing with undefined behavior.

## 8. Portability

- ISO C99
- `Makefile` build
- No platform-specific APIs required for core simulation logic

