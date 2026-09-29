# BronzeSim to ApeSDK adaptation boundary

## Decision

Treat BronzeSim as an **economic/cultural scenario package**, not as a second
generic simulation engine and not as a replacement for ApeSDK's biological
being model.  Move its portable, generic building blocks into ApeSDK only when
they improve ApeSDK independently of the Bronze Age scenario.  Keep authored
vocations, recipes, resource semantics, settlement rules, and the `.bronze`
language in a separate `bronze/` package.

This avoids making `sim/`, `entity/`, or `script/` depend on words such as
`copper`, `farmer`, or `settlement` while still allowing a Bronze scenario to
use ApeSDK landscape, time, being movement, deterministic loops, persistence,
and rendering.

## What exists today

BronzeSim has six useful concerns, although the current source files do not
quite preserve these boundaries:

| Concern | Current source | Keep / move |
| --- | --- | --- |
| Core utility containers, RNG, positions | `brz_vec.*`, `brz_util.*`, `brz_types.h`, `brz_kinds.*` | Fold only missing generic pieces into ApeSDK `toolkit/`; prefer existing ApeSDK types, vectors, file I/O, and deterministic RNG APIs. |
| Scenario language and compiled model | `brz_parser.*`, `brz_dsl.*`, `brz_dsl.h` | `bronze/dsl/`; do not merge with ApeScript. |
| Scenario economy and culture | `example.bronze`, kinds, vocations, recipes, resource rules | `bronze/content/`; completely data-defined. |
| Economic actors and settlements | `brz_agent.*`, `brz_settlement.*` | `bronze/economy/`, with an adapter to ApeSDK beings/groups. |
| Resource overlay and terrain queries | `brz_world.*`, `brz_land.*` | `bronze/world/` over ApeSDK `sim/land`, `weather`, and time. |
| Orchestration, reporting, and visual adapter | `brz_sim.*`, `main.c`, `bronzevis-mac/` | `bronze/runtime/`, `bronze/transfer/`, and a toolchain adapter. |

The 63 current vocation definitions fall naturally into content taxonomies:

* subsistence (15): farming, herding, fishing, hunting, and foraging;
* production (23): metal, pottery, fibre, leather, wood, stone, and tool work;
* infrastructure and exchange (12): buildings, boats, landing, water, fire,
  preservation, and trade;
* civic/ritual/defence (9); and
* life stage (3): infant, child, elderly.

Those labels should become `occupation.category` metadata in content, not C
enums.  The runtime should care only about capabilities, inputs, outputs,
locations, and effects.

## Target dependency direction

```
ApeSDK toolkit  <-  sim (terrain, weather, clock)  <-  entity (being hooks)
       ^                         ^                         ^
       |                         |                         |
       +-------------------------+-------------------------+
                                 |
                       bronze/adapter
                         /        \\
               bronze/world    bronze/economy
                         \\        /
                      bronze/dsl (parser + validator)
                                 |
                    bronze/content/*.bronze
                                 |
                     bronze/runtime + GUI/CLI adapters
```

`bronze/` may depend on ApeSDK.  ApeSDK's existing generic directories must
not depend on `bronze/`.

## Explicit interfaces

Build the adaptation around narrow interfaces, rather than passing
`ParsedConfig`, `BrzWorld`, or `simulated_being` through all layers.

* `BronzeWorldPort`: query terrain traits, find a target, reserve/take a
  resource, advance regeneration, and obtain clock/weather state.  The first
  implementation can emulate BronzeSim's tags; the ApeSDK implementation
  maps terrain and tide/weather values from `sim/land.*` and `sim/sim.h`.
* `BronzeActorPort`: get/set position, energy/fatigue, goal, and inventory;
  enumerate nearby actors.  It adapts a `simulated_being` plus Bronze-specific
  sidecar state.  Do **not** add economic inventories or occupations to
  `simulated_being` until they prove useful outside this scenario.
* `BronzeSettlementPort`: named fixed places, communal stores, population,
  policies, and exchange.  This remains Bronze-domain state.
* `BronzeEventSink`: typed events (`gathered`, `crafted`, `traded`, `rested`,
  `occupation_selected`) for snapshots, CSV/JSON transfer, tests, and a GUI.

The existing explicit seam in ApeSDK is promising: entity exposes movement,
location, drives, goals, and override hooks, while `universe/` supplies the
group loop and outer simulation cycle.  Use that capability, not a copy of
the ApeSDK lifecycle.

## Required content-model extraction

The current parser correctly represents task/rule structure, but its executor
still embeds Bronze content in C.  Before an ApeSDK integration, replace every
such name check with data in the compiled scenario:

| Current hard-coded policy | Replace with content data |
| --- | --- |
| `fish -> coast`, `grain -> field`, named mines/forest | resource habitat/query constraints |
| bronze, charcoal, pottery recipes | recipe records with arbitrary inputs and outputs |
| grain/fish auto-eating | food/nutrition definitions and diet policy |
| automatic grain/fish delivery | settlement stock policy or an explicit `deposit` task |
| scarcity price based on array index | resource-specific market target/valuation |
| hunger/fatigue-only conditions | typed actor/world/settlement variables exposed to the DSL |
| `move_to`, `roam`, `wander`, `gather`, `craft`, `trade`, `rest` branches | registered action handlers with typed arguments |

In particular, preserve *source locations* in compiled actions and conditions.
That will make invalid references and failed actions report the authored file
and line instead of silently becoming no-ops.  Validation should reject an
unknown action, resource, recipe, habitat, rule target, or variable before a
simulation starts.

## What should and should not move into ApeSDK

Move/adapt:

* terrain sampling and deterministic-land comparison work into an adapter over
  `sim/land.*`, rather than maintaining a second landscape generator;
* the Bronze day phase through ApeSDK's clock/weather cycle;
* agent location, movement, energy/fatigue, social proximity, deterministic
  iteration, state transfer, and rendering access through existing ApeSDK
  `entity/`, `universe/`, `toolkit/`, and `render/` facilities;
* only general utilities that ApeSDK lacks, after unit tests demonstrate the
  need.

Do not move:

* the `.bronze` grammar into `script/`.  ApeScript is an imperative control
  language; Bronze DSL is a declarative occupation/economy language.  A shared
  lexer can be considered later, but their ASTs and evaluators should remain
  separate;
* `BrzAgent` as a competing agent type, or Bronze resource arrays into the
  core `simulated_being` structure;
* `BrzWorld`'s full parallel tile store or `brz_land.*` into ApeSDK.  ApeSDK
  already owns landscape, weather, time, and map coordinates;
* the current CLI snapshot writer as a second generic serializer.  Map typed
  Bronze events/state into ApeSDK transfer/object facilities instead.

## Migration order and acceptance gates

1. **Freeze behavior.** Add deterministic golden tests for parsing, a short
   scenario run, inventories, settlement stores, and emitted events.  Record
   seed and tick/date mapping.
2. **Extract content.** Extend the DSL model with resources, habitats,
   recipes, nutrition, settlement policy, and action schemas.  Delete the
   named resource/recipe/diet/delivery branches from `brz_agent.c` and
   `brz_world.c`.  Existing `.bronze` content should reproduce the baseline.
3. **Make BronzeSim internally layered.** Introduce the four ports above and
   make its present world/agent/settlement implementations satisfy them.  This
   is the proof that the interfaces are adequate before involving ApeSDK.
4. **Create `apesdk/bronze/`.** Port parser/compiler/content/economy/runtime
   there, initially with a small standalone test harness linked to ApeSDK
   toolkit and `sim/` queries.  Keep it buildable independently from macOS
   toolchains.
5. **Attach to ApeSDK.** Replace the local world port with a land/weather/time
   adapter, then connect actors to `simulated_group` and entity movement/drive
   hooks.  Decide an explicit conversion between Bronze daily turns and ApeSDK
   cycles; never call a whole Bronze day once per arbitrary render frame.
6. **Integrate presentation and transfer.** Render content categories and
   event/state overlays in a dedicated toolchain adapter; add Bronze state to
   versioned transfer rather than changing existing Ape save compatibility.

A phase is complete only when the same seed gives stable event traces and all
existing BronzeSim tests plus the new ApeSDK-facing tests pass.  The current
BronzeSim suite passes 1,701 assertions; it is a useful baseline but needs
event/economy integration tests before it can guard the port.

## First implementation slice

Start with one minimal vertical slice: farmer, fisher, and bronze smith;
resources grain, fish, wood, copper, tin, charcoal; one settlement; and the
actions move, gather, craft, deposit, eat, and rest.  This covers terrain,
inventory, nutrition, recipes, a shared store, and a full production chain.
Only then bring in the remaining vocation categories.  It keeps the first
ApeSDK adapter small enough to verify while avoiding the misleading success of
porting dozens of labels that still execute the same generic `gather` action.
