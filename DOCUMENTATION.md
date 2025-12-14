# BRONZESIM Codebase Documentation

This document provides a **deep, code-aware** explanation of the BRONZESIM engine,
organized by *execution flow* and then by *module*. It is written to help new
contributors quickly understand where to make changes safely.

> Tip: keep this file open while reading the code; every section references the
> real filenames and the public entry points declared in headers.

---

## 1. High-level execution flow

The program starts in `main.c` and proceeds through four distinct stages:

1. **Parse DSL** → `brz_parse_file()` (`brz_parser.c`)
2. **Initialize runtime** → `sim_init()` (`brz_sim.c`)
3. **Run simulation days** → loop calling `sim_step()` (`brz_sim.c`)
4. **Write outputs** → snapshots / maps (`brz_sim.c` helpers)

At runtime, these subsystems interact:

- `brz_world.*` provides deterministic world sampling (tags + resources).
- `brz_cache.*` provides chunked loading so the full world is never stored.
- `brz_dsl.*` provides name→enum translation and DSL lookups.
- `brz_sim.*` defines agents, inventories, and day-step logic.

---

## 2. File-by-file guide

### 2.1 `main.c` — CLI entry point

**Purpose:** parse arguments, load DSL, run simulation.

Key responsibilities:
- Read `.bronze` file path from argv
- Call `brz_parse_file(path, &cfg)`
- Construct `WorldSpec` / `WorldGen` configuration from parsed parameters
- Call `sim_init(...)`
- For `cfg.days` iterations, call `sim_step(&sim)`
- Optionally write periodic snapshots / maps (based on `snapshot_every` and `map_every`)

When changing CLI behavior (new flags, new outputs), keep *all simulation behavior* out of `main.c`.

---

### 2.2 `brz_parser.h/.c` — DSL parsing

Public entry point:
- `bool brz_parse_file(const char* path, ParsedConfig* out_cfg);`

#### 2.2.1 Lexer model

The lexer is intentionally minimal:
- It recognizes **words** and **braces**
- Comments start with `#`

That means the parser is essentially a structured reader over word streams.

#### 2.2.2 Top-level blocks

The parser recognizes these blocks (anything else is skipped if braced):

- `sim { ... }`
- `agents { count N }`
- `settlements { count N }`
- `resources { ... }`
- `vocations { ... }`

#### 2.2.3 Vocation parsing

Inside `vocations { ... }`, every entry must be a `vocation`:

- `vocation <name> { ... }`

Inside a vocation, only:
- `task ...`
- `rule ...`

are permitted.

#### 2.2.4 Task parsing

Task body lines are parsed as **operations**:

- `move_to <tag>`
- `gather <resource> <int>`
- `craft <item> <int>`
- `trade`
- `rest`
- `roam <int>`

These map directly to `OpKind` values.

#### 2.2.5 Rule parsing + conditions

Rule syntax:

- `rule <name> { when <clauses> do <task> weight <int> [prob <float>] }`

Implementation nuance:
- condition parsing consumes **through** the `do` token
- the task name comes immediately after

Supported clauses:
- `hunger > <float>`
- `fatigue < <float>`
- `season == <season>`
- `inv <item> <cmp> <int>`
- `prob <float>`

---

### 2.3 `brz_dsl.h/.c` — DSL data model + semantic helpers

This module does **not** parse. It provides:

- the in-memory schema (`VocationDef`, `TaskDef`, `RuleDef`, `OpDef`, etc.)
- name resolution helpers:
  - `voc_find`, `voc_get`
  - `voc_task`, `voc_task_mut`
- keyword/enum parsing:
  - `dsl_parse_resource`
  - `dsl_parse_item`
  - `dsl_parse_tagbit`

Practical guidance:
- When you add a new DSL keyword (resource/item/tag), update this module.
- When you add a new op kind, update:
  - `OpKind` enum
  - parser operation parsing
  - simulation execution logic

---

### 2.4 `brz_world.h/.c` — deterministic world generation

The world is too large to store as a full grid. Instead, this module provides
pure sampling functions:

- `world_cell_tags(...)` returns terrain tags for a cell (bitfield).
- `world_cell_res0(...)` returns a baseline resource quantity for a cell.
- `world_season_kind(day)` maps day → season.

The design is “stateless sampling”: values are derived from seed + coordinates.

---

### 2.5 `brz_cache.h/.c` — chunk cache + LRU eviction

Because the world is huge, BRONZESIM loads terrain into **chunks** and keeps
only the working set in memory.

Public API:
- `cache_init`, `cache_destroy`
- `cache_get_chunk`
- `cache_get_cell`
- `cache_regen_loaded` (seasonal regeneration)

If you see memory growth or slowdowns, this is where you investigate first.

---

### 2.6 `brz_sim.h/.c` — simulation engine

This is the heart of the project.

Public API:
- `sim_init(Sim* s, ...)`
- `sim_step(Sim* s)`
- `sim_destroy(Sim* s)`
- output helpers (snapshot/map)

Core ideas implemented here:
- Agents belong to settlements.
- Each agent has state (hunger, fatigue, inventory, etc.).
- Each day:
  - Choose a task based on rules whose conditions match.
  - Execute task operations sequentially.
  - Apply resource regeneration via `cache_regen_loaded`.

This file is also where you will find:
- task selection logic (weights + optional `prob`)
- inventory updates
- movement / location preference

---

### 2.7 `brz_util.h/.c` — utilities

Utilities are kept small and explicit:
- `brz_streq` (string equality)
- hash/mixing helpers (for deterministic sampling)
- clamping
- panic/assert handling

---

## 3. Where to change things safely

### Add a new resource:
1. Add enum entry in `ResourceKind` (`brz_world.h`)
2. Update `dsl_parse_resource` mapping (`brz_dsl.c`)
3. Add renew key handling in parser (`resources { ... }`)
4. Ensure cache/world sampling handles it consistently

### Add a new operation:
1. Add enum in `OpKind` (`brz_dsl.h`)
2. Extend `parse_task` in `brz_parser.c`
3. Implement execution in `brz_sim.c`
4. Document it in `DSL_MANUAL.md`

### Improve rule conditions:
1. Extend `Condition` struct (`brz_dsl.h`)
2. Extend `parse_condition` (`brz_parser.c`)
3. Extend evaluation logic (`brz_sim.c`)

---

## 4. Error handling philosophy

The project assumes DSL authors are collaborators, not attackers.

- Bad DSL triggers assertions.
- This keeps the engine simple and prevents silent divergence.

If you want “friendly errors”, add them *without* weakening invariants:
- file + line reporting
- structured parse errors
- optional “validate-only” mode

