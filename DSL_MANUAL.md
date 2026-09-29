# BronzeSim DSL Manual

The BronzeSim DSL defines a deterministic economic and cultural scenario. A
scenario supplies the world settings, resource ecology, recipes, settlement
policy, and occupations; the runtime supplies generic action handlers.

Use [`src/example.bronze`](src/example.bronze) as the complete working
reference. It contains the current 63-vocation Bronze Age scenario.

## Quick start

```bronze
kinds {
  resources { grain }
  items { }
}

actions {
  action gather 1 1
}

resources {
  resource grain {
    habitat field
    capacity 100
    renew 0.10
    nutrition 0.20
    market_target 50
  }
}

vocations {
  vocation farmer {
    task work {
      gather grain 2
    }

    rule work_when_hungry {
      when hunger > 0.25 do work weight 1
    }
  }
}
```

Build and run a scenario:

```sh
make -C src
./src/bronzesim path/to/scenario.bronze
```

## Writing style and lexical rules

Use lower_snake_case names. Identifiers start with a letter or underscore and
may then contain letters, digits, or underscores. They are case-sensitive.

Numbers are non-negative integers or decimals such as `2`, `0.25`, and `100.0`.
Quoted strings and negative numeric literals are not supported by the current
lexer.

Use `#`, `//`, or `/* ... */` comments. Braces delimit blocks. Semicolons,
commas, and colons are accepted as visual separators and otherwise ignored.

Put one action on each physical line. The task parser uses the line break to
find the end of an action, so this is significant:

```bronze
task work {
  move_to field
  gather grain 2
}
```

## Top-level blocks

| Block | Purpose |
| --- | --- |
| `world` | Seed and world settings. |
| `sim` | Runtime settings such as day count and report frequency. |
| `agents` | Agent count. |
| `settlements` | Settlement count. |
| `kinds` | Resource and item registries. |
| `resources` | Resource definitions and legacy renewal values. |
| `items` | Compatibility item registry entries. |
| `actions` | Action names and their word-argument contracts. |
| `recipes` | Material transformations. |
| `settlement_policy` | Shared food, deposit, and rest behaviour. |
| `vocations` | Occupations, tasks, and rules. |

### World and runtime settings

`world`, `sim`, `agents`, and `settlements` are key/value blocks. The runtime
recognises these common values:

```bronze
world {
  seed 1337
  sim_map_w 160
  sim_map_h 80
}

sim {
  days 180
  report_every 30
  snapshot_every 0
  map_every 0
}

agents { count 900 }
settlements { count 12 }
```

The seed controls deterministic world generation and rule randomness. The same
scenario and seed produce the same event sequence.

### Kinds and items

Declare resources and items before referencing them elsewhere:

```bronze
kinds {
  resources { fish grain wood clay copper tin charcoal }
  items { tool pot }
}
```

The compatibility form below also registers items:

```bronze
items {
  pot item
  tool item
}
```

Do not attach economic meaning to declaration order. Habitat, food value, and
market targets are properties of resource definitions.

### Resources

Use a full definition for every resource with world or economic behaviour:

```bronze
resources {
  resource fish {
    habitat coast
    capacity 200
    renew 0.12
    nutrition 0.20
    market_target 100
  }
}
```

`habitat` is the terrain query used by gathering and movement. The current
world recognises `coast`, `field`, `forest`, `claypit`, `mine_copper`,
`mine_tin`, and `fire`.

`capacity` is availability at a matching location. `renew` is the daily
proportion restored toward capacity. `nutrition` is hunger relief per consumed
unit; zero means the resource is not food. `market_target` is the settlement
stock level used for scarcity pricing.

This legacy renewal-only form remains valid for existing scenarios:

```bronze
resources {
  fish_renew 0.12
}
```

### Actions

Declare each action a scenario may use. The two numbers are the minimum and
maximum count of word arguments; a numeric quantity is not counted.

```bronze
actions {
  action gather 1 1
  action craft 1 1
  action move_to 1 1
  action rest 0 0
  action trade 0 2
}
```

The current runtime handles `gather`, `craft`, `trade`, `rest`, `move_to`,
`roam`, and `wander`. When an `actions` block exists, using an undeclared
action is a validation error.

### Recipes

A recipe has one output and one or more inputs. Inputs and outputs can be
registered resources or items.

```bronze
recipes {
  recipe tool {
    output tool 1
    input copper 1
    input tin 1
    input charcoal 1
  }

  recipe pot {
    output pot 1
    input clay 2
  }
}
```

`craft tool 1` runs the `tool` recipe. Insufficient inputs are recoverable: the
action is valid but produces no material.

### Settlement policy

```bronze
settlement_policy {
  food_fallback grain
  deposit_threshold 2
  rest_recovery 0.04
}
```

Food resources above `deposit_threshold` are deposited at an agent's home
settlement. `rest_recovery` is the fatigue reduction at home. `food_fallback`
is retained for future policy extensions; current food selection uses positive
resource `nutrition` values.

## Vocations, tasks, and rules

A vocation is an occupation. It owns named tasks and weighted rules. Agents
receive vocations deterministically when they spawn.

```bronze
vocations {
  vocation fisher {
    task work {
      move_to coast
      gather fish 3
    }

    rule hungry_work {
      when hunger > 0.25 and fatigue < 0.90
      do work
      weight 6
    }
  }
}
```

### Tasks

A task runs statements in order. The core actions use these forms:

```bronze
gather RESOURCE AMOUNT
craft RECIPE AMOUNT
trade GIVE WANT
rest
move_to HABITAT
roam [HABITAT]
wander [HABITAT]
```

Tasks may contain conditional and probabilistic blocks:

```bronze
task cautious_work {
  when fatigue < 0.80 {
    gather grain 2
  }
  chance 25 {
    rest
  }
}
```

`chance` uses a percentage from `0` through `100`.

### Rules and conditions

A rule selects one task or a declared zero-argument action:

```bronze
rule hungry_work {
  when hunger > 0.25 and fatigue < 0.90
  do work
  weight 6
}
```

Conditions support `hunger`, `fatigue`, `true`, `false`, `prob`, and
`chance(...)`; they support `>`, `<`, `>=`, `<=`, `==`, `!=`, `and`, `or`, and
parentheses. `prob 0.25` and `chance(0.25)` both use probabilities from `0`
through `1`.

All passing rules participate in weighted selection. A missing weight means
`1`. A rule target must name a task in the same vocation or a declared action.

## Validation and runtime behaviour

The parser first checks syntax, then validates configuration references before
simulation begins. It rejects unknown recipe inputs or outputs, resources that
are absent from the registry, undeclared actions, invalid action argument
counts, and missing rule targets.

Diagnostics use `SyntaxError:<line>:<column>` and `ValidationError:<line>`.
At runtime, depleted resources and insufficient recipe inputs are recoverable.
The event interface records selection, gathering, crafting, trading, resting,
and deposits for deterministic tests, snapshots, and adapters.

## Formal grammar

The following section is generated from the authoritative grammar block in
`src/brz_dsl.c`. After changing that block, run:

```sh
python3 tools/extract_dsl_grammar.py
python3 tools/update_docs.py
```

<!-- AUTO-GENERATED-GRAMMAR-BEGIN -->

```ebnf
# NOTE: This block is the single source of truth for the BRONZESIM DSL grammar.
# It is extracted and injected into DSL_MANUAL.md automatically (make -C src docs).
#
# Conventions:
#   - 'literal' denotes a keyword or symbol token.
#   - identifier and number are lexical tokens.
#   - { X } means repetition (zero or more).
#   - [ X ] means optional.
#
# This grammar describes the *surface syntax*. The engine imposes additional semantic rules.

program             := { top_level_block } EOF ;

top_level_block     := world_block
                    | kinds_block
                    | resources_block
                    | items_block
                    | recipes_block
                    | actions_block
                    | settlement_policy_block
                    | vocations_block
                    | compat_block ;

# ----- Blocks -----

world_block          := 'world' block_open { world_stmt } block_close ;
kinds_block          := 'kinds' block_open { kind_def } block_close ;
resources_block      := 'resources' block_open { resource_def } block_close ;
resource_def         := identifier number
                    | 'resource' identifier block_open { resource_property } block_close ;
resource_property    := 'habitat' identifier
                    | ('capacity' | 'renew' | 'nutrition' | 'market_target') number ;
items_block          := 'items' block_open { item_def } block_close ;
recipes_block        := 'recipes' block_open { recipe_def } block_close ;
recipe_def           := 'recipe' identifier block_open 'output' identifier number { 'input' identifier number } block_close ;
actions_block        := 'actions' block_open { 'action' identifier number number } block_close ;
settlement_policy_block := 'settlement_policy' block_open { settlement_property } block_close ;
settlement_property  := 'food_fallback' identifier
                    | ('deposit_threshold' | 'rest_recovery') number ;

vocations_block      := 'vocations' block_open { vocation_def } block_close ;
vocation_def         := 'vocation' identifier block_open { vocation_member } block_close ;
vocation_member      := task_def | rule_def ;

task_def             := 'task' identifier block_open { task_stmt } block_close ;
rule_def             := 'rule' identifier block_open { rule_stmt } block_close ;

# ----- World statements -----
# The world block is intentionally permissive: keys are identifiers.
# Values can be number or identifier.

world_stmt           := identifier value ;
value                := number | identifier ;

# ----- Registry definitions -----

kind_def             := 'resources' block_open { identifier } block_close
                    | 'items' block_open { identifier } block_close ;
item_def             := identifier 'item' ;

# ----- Rule / task language -----

rule_stmt            := 'when' condition
                    | 'do' identifier
                    | 'weight' number ;

task_stmt            := action_stmt | when_block | chance_block ;

# Common structured statements
when_block           := 'when' condition block_open { task_stmt } block_close ;
chance_block         := 'chance' number block_open { task_stmt } block_close ;

# Conditions are intentionally simple in the core grammar.
# The engine may accept additional operators in future revisions.

condition            := comparison | 'true' | 'false' | 'prob' number | 'chance' '(' number ')' ;
comparison           := identifier cond_op cond_rhs ;
cond_op              := '<' | '<=' | '>' | '>=' | '==' | '!=' ;
cond_rhs             := number | identifier ;

# Actions are a small, engine-defined set of verbs.
# Extend the verb set in the engine and keep the grammar here in sync.

action_stmt          := identifier { identifier | number } ;

# ----- Lexical helpers -----

block_open           := '{' ;
block_close          := '}' ;

# ----- Compatibility blocks -----
# Older examples may use these blocks. They are accepted for backwards compatibility
# and may be mapped internally onto the newer registries.

compat_block         := ('sim' | 'agents' | 'settlements') block_open { identifier value } block_close ;
```

<!-- AUTO-GENERATED-GRAMMAR-END -->
