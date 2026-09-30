# BronzeSim–Iraqi Civilian Alignment Plan

## Recommendation

Yes: align the two simulations **before** expanding ApeSDK work.  Make the
Bronze DSL the shared scenario-authoring layer, supported by a small
scenario-runtime contract, rather than merging the simulations or moving
domain content into ApeSDK.

BronzeSim and the Mosul civilian prototype have the same useful shape:

```
authored scenario -> deterministic stepper -> agents + places + resources
                  -> typed events -> narration / save / visual adapter
```

They should converge at that seam.  Their scenarios should remain distinct:
BronzeSim is an Early Bronze Age economy over a generated island; Mosul is a
fictional, non-predictive civilian mobility/economy exercise over a named
district.  ApeSDK should remain the optional provider of generic being,
episodic-memory, terrain/time, transfer, and presentation facilities.

Do not copy Mosul's map, fictional household prices, profession list, or
narrative wording into BronzeSim.  Do not move the DSL, settlements,
vocations, or resource recipes into ApeSDK core directories.

## Findings at the planning baseline (2026-09-30)

### BronzeSim already has the right beginnings

- `src/brz_port.{h,c}` defines local world, actor, settlement, and event port
  types; `src/brz_sim.c` creates a world port; and `brz_run_with_events()`
  exposes a typed event sink.
- `src/brz_dsl.{h,c}` and `src/brz_parser.c` make resource habitat, capacity,
  regeneration, nutrition, market targets, recipes, action declarations, and
  parts of settlement policy authored data.  Validation already reports many
  source lines.
- The deterministic C99 test suite includes parsing, world, DSL, and one
  runtime event test.  `docs/APESDK_ADAPTATION.md` has a sound dependency
  direction and a vertical-slice proposal.

### Gaps to close in BronzeSim

- The executor still accepts `BrzWorld *`, `BrzAgent *`, and
  `BrzSettlement *` directly.  The ports are not the execution boundary yet.
- `src/brz_agent.c` still maps habitat strings to Bronze tags and branches on
  action spellings; condition variables are hard-coded to hunger/fatigue.
- Events lack a stable schema version, tick/minute, source location, place
  name, counterpart, outcome/status, and a safe owned payload.  `subject` is
  only a borrowed string.
- Snapshots are presentation JSON, not a versioned restart/checkpoint format;
  they omit the RNG stream, resource cells, targets, and enough state for an
  exact resume.
- The current settlement policy is still implicit in runtime loops (automatic
  eating, rest, and deposit).  This prevents Mosul-style staffed services,
  wallets, and deferred outcomes from being expressed consistently.
- BronzeVis is a separate direct renderer of Bronze structs, not an event/state
  consumer with a documented adapter boundary.

### Relevant Mosul lessons, not code to copy wholesale

`../iraqicivilian-mosul/c_simulation/` demonstrates the target capabilities:
deterministic checkpoints/replay tests, explicit scenario economics, named
workplaces, compact narrative/debug event modes, and an ApeSDK host bridge at
`apesdk_host_bridge.{h,c}`.  Its own plan also records unfinished consolidation:
the CMake path builds the bridge/runtime target while the Makefile still names
the older episodic adapter.  Treat the bridge API and its tests as reference
material; do not depend on that implementation until Mosul completes its
retirement audit.

## Target architecture

Keep each repository buildable on its own.  Introduce a small, C99-compatible
shared contract in a new repository or a versioned subtree such as
`scenario-runtime/`; until it is proven, keep its proposed headers duplicated
byte-for-byte in both projects and test compatibility in CI.

```
              Bronze content (.bronze)       Mosul scenario/map/policy
                       |                              |
                       +-------- scenario runtime ----+
                                      |
                   ports: world | actor | place | clock | event | state
                                      |
              local C adapters today / ApeSDK adapters later
                                      |
                  ApeSDK generic engine (no scenario dependency)
```

The shared runtime layer contains interfaces, schema definitions, deterministic
test fixtures, and serializers only.  The DSL compiler/content layer is its
own optional package.  Neither layer contains a resource enum, a historical
model, or UI code.

## DSL as the primary shared asset

BronzeSim's DSL is the best existing candidate for the common authoring
language: it already supports data-defined resources, recipes, action
declarations, tasks, weighted rules, needs conditions, source locations, and
validation.  It should be evolved into a generic `scenario` DSL with a Bronze
content profile and a Mosul content profile.  This is a semantic generalization,
not a rename-and-copy exercise.

### What a Mosul profile needs to express

- named, typed **places** (home, bakery, clinic, water point, school, mosque,
  workshop), capacity/queue/store policy, and a stable map/portal reference;
- **roles/professions**, workplace eligibility, skill/training/mentor policy,
  and age/lifecycle eligibility;
- actor **needs** (food, water, rest, health, social contact, work) and their
  prioritisation, rather than a hard-coded `hunger`/`fatigue` pair;
- **services and exchanges**: provider and recipient qualifications, inputs,
  outputs, price/levy/accounting policy, success/deferred/unstaffed outcomes;
- scenario **disruptions** such as a food shortage or water closure, expressed
  as explicit state/events rather than hidden branches;
- event presentation keys (for example `service.provided`) and place naming
  data.  Grammar and translated prose remain in the Mosul presentation adapter;
- external immutable asset references for the Mosul vector/navigation data.
  The DSL identifies a map, layer, site, or portal id; it does not encode SVG
  paths, Metal styling, or raw grid geometry.

### What must remain outside it

- ApeSDK's biological being layout, episodic replacement algorithm, and
  imperative ApeScript controls;
- BronzeSim's generated heightmap implementation and Mosul's vector geometry
  generation/render pipeline;
- UI command parsing, Swift/Metal/CoreGraphics rendering, and prose templates;
- arbitrary C callbacks or executable expressions in authored content.

### DSL migration rule

Keep `.bronze` as the extension initially, but define a language version and
`scenario_kind`/profile field.  Add the generic AST/compiler beside the current
parser, translate the existing Bronze syntax into it, and add `mosul.scenario`
only after the shared semantics are tested.  Do not make the current task
executor silently reinterpret existing Bronze content.

## Common contract to design first

### 1. Clock, identity, and deterministic execution

Define fixed-width types for `ScenarioTick`, `ScenarioActorId`, `ScenarioPlaceId`,
`ScenarioSeed`, and a versioned tick-to-calendar mapping.  A step must receive
an explicit tick/time and RNG state, never derive time from a renderer frame.
Specify deterministic iteration order and how random draws are partitioned so
adding a visual observer cannot change outcomes.

### 2. Ports

Evolve `brz_port.h` into opaque, capability-based interfaces:

- `ScenarioWorldPort`: terrain/query traits, path/target query, resource
  reserve/take/restore, regeneration, and clock/weather access.
- `ScenarioActorPort`: stable identity, position, movement request/result,
  needs, inventory operations, and nearby-actor enumeration.
- `ScenarioPlacePort`: named/home/work place lookup, shared stores, capacity,
  queue/service availability, and scenario-defined policy data.
- `ScenarioStatePort`: versioned save/load hooks for scenario-owned state.

Use result codes (`ok`, `unavailable`, `blocked`, `invalid`, `deferred`) rather
than silently falling back to a forest tag or emitting a successful trade when
no exchange occurred.  BronzeSim may retain tags internally; the contract must
use named/queryable traits rather than `BRZ_TAG_*` bit values.

### 3. Events and observation

Replace `BronzeEvent` with a versioned `ScenarioEvent` envelope: schema
version, tick, event kind, actor id, optional counterpart id, optional place
id, action/content id, requested and completed amount, result code, and source
file/line where applicable.  The sink receives immutable values whose text is
owned or interned for the event lifetime.

Map Bronze events such as `gathered`, `crafted`, `deposited`, `traded`,
`rested`, and `occupation_selected` into that envelope.  Keep Mosul narration
as a presentation adapter: normal prose and debug output must be derived from
events, never become a second behavioural store.

### 4. Versioned state

Define a portable, explicit state header (magic, schema version, scenario id,
content hash, seed, tick, payload length/checksum).  Checkpoint payloads remain
scenario-owned.  Loading an incompatible content version must fail clearly;
there must be a named migration function for any supported old version.

## Phased implementation

### Phase 0 — establish comparable evidence

**BronzeSim files:** `src/test/test_sim.c`, new event-test helper, `src/Makefile`.

1. Record a short fixed-seed baseline of event trace, inventories, settlement
   stores, positions, hunger/fatigue, and RNG/tick convention.
2. Add tests for event order and payloads, recipe success/insufficient inputs,
   successful versus unavailable trade, resource regeneration, and a 30-day
   golden trace.  Avoid asserting console formatting.
3. Add `make test` at the repository root that invokes `src/test`; document
   the command and expected test count in this plan or README.

**Gate:** same seed and content produce byte-identical canonical trace on two
runs; an event observer cannot alter simulation state.

### Phase 1 — finish BronzeSim's internal boundary

**BronzeSim files:** `src/brz_port.{h,c}`, `src/brz_agent.{h,c}`,
`src/brz_sim.c`, `src/brz_world.{h,c}`, `src/brz_settlement.{h,c}`.

1. Change task execution to receive ports and a step context instead of direct
   `Brz*` structures.  Local adapters must retain current behaviour.
2. Make world/resource operations transactional: query/reserve/take/report.
   Emit a completed amount only after a successful take.
3. Add actor inventory and place-store operations to their respective ports;
   remove direct inventory/store access from action handlers.
4. Move movement outcome, target lookup, and bounds handling behind the world/
   actor ports so a street graph or ApeSDK terrain adapter can implement them.

**Gate:** no executable action function in `brz_agent.c` takes `BrzWorld *` or
`BrzSettlement *`; all existing deterministic baselines remain unchanged.

### Phase 2 — extract the generic DSL compiler and add a Mosul pilot

**BronzeSim files:** `src/brz_dsl.{h,c}`, `src/brz_parser.c`, new
`src/scenario_{ast,compile,validate}.{h,c}`, tests and DSL manual.

1. Version the syntax and compile the current Bronze declarations into a
   generic scenario model while preserving exact current semantics and source
   diagnostics.
2. Add generic declarations for places, roles, actor state/needs, services,
   policies, disruptions, and external map references.  Names resolve to typed
   ids during validation; runtime adapters receive ids, never parser strings.
3. Create one small Mosul DSL fixture: a household, bakery, water point,
   clinician, adult/child roles, food/water service, one deferred request, and
   one disruption.  Its test must execute through a temporary local adapter,
   not through the full vector viewer.
4. Compile the same fixture in Mosul's C test target before attempting a full
   content migration.  Retain Mosul's present hand-authored setup as the
   behaviour oracle during this transition.

**Gate:** the existing `example.bronze` and the Mosul fixture produce stable
typed action/event plans; validation rejects an invalid profession, place,
service input, map reference, or state variable with a source location.

### Phase 3 — shared runtime-contract pilot with Mosul

**Both repos:** new `scenario-runtime` header/test package; no ApeSDK change.

1. Agree on the smallest versioned event envelope, clock, result codes, and
   port ABI described above.  Write C99 compile tests from both repositories.
2. Write one canonical fixture for an actor travelling to a place, requesting a
   resource/service, receiving either `completed` or `deferred`, and emitting
   events.  Run it against a Bronze local adapter and Mosul local adapter.
3. Share test vectors and schema documentation, not concrete actor structs.
4. Choose ownership only after the pilot: a separate small repository is
   preferable; an ApeSDK `scenario/` extension is acceptable only if it stays
   optional and no core module includes it.

**Gate:** both adapters compile against the same header and produce equivalent
canonical event/state fixtures for the shared test case.

### Phase 4 — compile and validate scenario semantics

**BronzeSim files:** `src/brz_dsl.{h,c}`, `src/brz_parser.c`, `src/brz_agent.c`,
`src/example.bronze`, DSL/manual/tests.

1. Compile strings in actions, resources, habitat queries, recipes, rules, and
   conditions into validated ids/typed operands before execution.  Preserve
   source file and line on compiled records.
2. Replace `tag_for_habitat()` with content-declared terrain queries resolved
   by the world adapter.  An unknown habitat, action, variable, resource,
   recipe, task, or place fails parsing/validation—not at runtime.
3. Replace action-name `if` chains with a small registered handler table keyed
   by compiled action id.  Initial handlers may still be `move`, `gather`,
   `craft`, `exchange`, `consume`, `deposit`, and `rest`.
4. Make food, deposit, recovery, capacity, service, and market rules explicit
   policy/action data.  Preserve compatibility syntax only as a translated,
   documented legacy form.

**Gate:** a deliberately invalid scenario reports source location and has no
side effects; `example.bronze` recreates Phase 0 results.

### Phase 5 — checkpoint and observability alignment

**BronzeSim files:** new `src/brz_state.{h,c}`, `src/brz_sim.c`, CLI/tests,
BronzeVis adapter.

1. Implement checkpoint save/load from the scenario-state port, including
   agents, settlement stores, world resource state, targets, RNG, content
   identity, and current tick.
2. Add interrupted/resumed replay: a full run and save/resume run must produce
   byte-identical checkpoint and canonical event trace.
3. Add normal event narration and debug/JSON output as separate sinks.  Normal
   mode should name vocation/action/place; debug mode may show ids and amounts.
4. Change BronzeVis to consume a documented read-only state snapshot and event
   feed rather than reach through into mutable engine structures.

**Gate:** BronzeSim gains Mosul's essential operational guarantees—replay,
inspectable events, and clear incompatible-save diagnostics—without sharing
Mosul's scenario data.

### Phase 6 — add ApeSDK adapters only after the above gates

**Location:** an optional Bronze extension/adapter, never ApeSDK core.

1. Start with the narrow farmer/fisher/smith vertical slice already proposed in
   `docs/APESDK_ADAPTATION.md`; use one settlement and six actions.
2. Implement `ScenarioWorldPort` over ApeSDK land/weather/clock, then
   `ScenarioActorPort` over a `simulated_being` plus Bronze sidecar state.
   Do not add occupation arrays or market inventories to `simulated_being`.
3. Use the Mosul host bridge as the pattern for stable identity, location,
   read-only episodic views, and presentation callbacks.  Reuse actual
   ApeSDK profession/episodic APIs only where their semantics match; Bronze
   economic events remain scenario events.
4. Add an explicit daily-turn-to-ApeSDK-cycle conversion and tests.  Rendering
   must not determine simulation cadence.
5. Map versioned Bronze state/events through an adapter; do not change existing
   ApeSDK save compatibility until the extension's own migrations are proven.

**Gate:** the vertical slice has a local and an ApeSDK-backed run with stable,
auditable event traces.  Only then expand vocation coverage.

## Deliberate non-goals

- A single DSL: `.bronze` remains declarative scenario content; ApeScript
  remains imperative control.  They may share low-level lexer utilities later,
  but not AST/evaluator semantics.
- A merged world model: generated island terrain and Mosul's authored
  multi-layer, portal-aware district remain separate world adapters.
- A common UI: BronzeVis and the Mosul Swift/Metal viewer can consume common
  snapshots/events while retaining their appropriate visual grammars.
- Importing real-world claims: Mosul's names, prices, and civilian policy stay
  explicitly fictional and scenario-local.

## First next change

Phase 0 implementation was added on 2026-09-30; verification is pending
acceptance of this host's Xcode command-line-tools license.  The root `make
test` target runs the C99 suite.  Its Phase 0 coverage records a fixed-seed
canonical event trace and checks recipe inputs/outputs, unavailable-trade
state, regeneration cap, and repeatability.  The unavailable-trade trace
deliberately records the current emitted `traded` event despite no inventory
movement; Phase 1 must replace that ambiguous outcome with a result code.

## Phase 1 status

Implemented 2026-09-30; verification remains pending acceptance of this
host's Xcode command-line-tools license.  The internal-boundary work changes
`brz_agent_step()` and all task execution helpers to accept `BronzeWorldPort`
and `BronzeSettlementPort`, rather than `BrzWorld *` and `BrzSettlement *`.
The local adapters now provide position clamping, named-settlement lookup,
resource/item store mutation, and scarcity pricing.  The CLI runner, the
Phase 0 direct-step tests, and BronzeVis construct and own those ports.

`BronzeActorPort` now owns action-time position/target, inventory, settlement
identity, and need access/mutation.  `BronzeEvent` carries an explicit result
(`completed`, `unavailable`, `deferred`, or `invalid`), and the unavailable
trade baseline now requires zero completed amount plus `unavailable`; the
local adapter no longer transfers the offered good when no requested stock is
available.  World gathering performs an availability/reservation/take flow;
the current single-threaded adapter documents reservation as advisory, while a
future concurrent adapter can make it durable.

This completes the Phase 1 implementation gate: executable action helpers do
not take `BrzWorld *` or `BrzSettlement *`, and local adapters preserve the
engine boundary.  Run `make test` after the Xcode license is accepted before
marking the phase verified.

## Phase 2 status

Started 2026-09-30.  The compiler now recognizes a versioned `scenario`
profile and generic `map_refs`, `places`, `roles`, `needs`, `services`, and
`disruptions` declarations.  `ParsedConfig` owns these compiled definitions;
its validation rejects unsupported language versions and unknown map, place,
role, service, or disruption references.  A compact Mosul fixture covers a
household, water point, civilian/water-technician roles, water/work needs,
staffed water provision, and water closure.

The declarations intentionally identify external navigation/map assets rather
than embedding geometry.  A commit-ready Mosul adapter now binds a named
generic service to its validated place and provider role, with a standalone
Mosul test fixture for water provision.  Runtime execution of generic service
policy in the full Mosul loop is intentionally deferred: Phase 2 establishes
the compiler/adapter seam, not a second population runtime.

Phase 2 was verified on 2026-09-30: BronzeSim's C99 suite passed 1,754 tests,
and the Mosul `test-scenario-dsl` adapter fixture passed.  The Mosul portable
Makefile also built the current host-bridge simulator successfully (with its
pre-existing compiler warnings).  The two repositories now have a validated
compiler/adapter seam; Phase 3 may begin.

## Phase 3 status

Completed 2026-09-30.  The shared C99 contract now lives in the source-only
`scenario-runtime/` package.  It defines stable tick, actor and place
identities; a versioned event envelope; read-only opaque world, actor, and
place views; and `completed`, `unavailable`, `deferred`, and `invalid` result
codes.
`bronze_event_to_scenario()` projects the existing Bronze event into that
envelope without exposing Bronze's mutable runtime structures.  Mosul's
scenario adapter constructs the same envelope directly from its validated
service binding.

The canonical fixture reads local adapter views, emits recipient travel at tick
60 and water provision at tick 61 (provider 7, recipient 8, place 2), then
checks the arrived place/position and delivered amount.  It runs once as
`completed` and once as `deferred`; both local adapters compile the same
header and compare the same completed state vector.  On 2026-09-30 BronzeSim's
suite passed 1,760 tests and Mosul's `make test-scenario-dsl` fixture passed.
The vector and ownership boundary are documented in `scenario-runtime/README.md`.

## Phase 4 status

Started 2026-09-30.  Parsing now compiles declared action names to stable
`BrzActionCode` values; resolves resource, item, recipe, and terrain-query
operands to typed ids; and rejects an unsupported action, unknown terrain
query, or undeclared action operand with the statement's source line. Recipe
inputs and outputs are likewise resolved before execution. The runtime routes
compiled action codes through a registered handler table and no longer falls
back from an unknown terrain name to forest. `food_fallback` is now honored as
the declared first-choice nutrition policy.

New parser coverage proves that unknown terrain and gather operands fail before
the simulation starts. `make test` passed with 1,764 tests on 2026-09-30.
The remaining Phase 4 work is to replace the free-form rule/`when` expression
evaluator with a typed condition representation and to move the remaining
automatic settlement defaults into explicit scenario policy declarations.
