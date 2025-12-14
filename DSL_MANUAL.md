# BRONZESIM DSL Manual

This document is the authoritative reference for the BRONZESIM **Domain‑Specific Language (DSL)** as implemented by the parser in `brz_parser.c`.

The DSL is intentionally small:

- **Not** Turing-complete
- **No** variables, loops, or functions
- Designed for **deterministic**, auditable simulation rules

---

## 1. Lexical structure

The lexer recognizes only four token kinds:

- `WORD` (any non-whitespace sequence that is not `{` or `}`)
- `{`
- `}`
- end-of-file

Comments:

- `#` starts a comment that runs to end-of-line
- Whitespace is ignored

Because everything is a `WORD`, operators like `>`, `<`, `==`, `>=`, `<=` are treated as words.

---

## 2. File structure

A `.bronze` file is a sequence of top-level blocks. Known blocks are:

- `sim { ... }`
- `agents { ... }`
- `settlements { ... }`
- `resources { ... }`
- `vocations { ... }`

Unknown blocks are allowed and will be skipped if they use braces.

---

## 3. EBNF grammar (implementation-aligned)

```ebnf
file            = { block } ;

block           = sim_block
                | agents_block
                | settlements_block
                | resources_block
                | vocations_block
                | unknown_block ;

sim_block        = "sim" "{" { sim_kv } "}" ;
sim_kv           = "seed" WORD
                | "days" WORD
                | "cache_max" WORD
                | "snapshot_every" WORD
                | "map_every" WORD
                ;

agents_block     = "agents" "{" "count" WORD "}" ;
settlements_block= "settlements" "{" "count" WORD "}" ;

resources_block  = "resources" "{" { resource_kv } "}" ;
resource_kv      = RESOURCE_NAME WORD ;          # float value

vocations_block  = "vocations" "{" { vocation_def } "}" ;

vocation_def     = "vocation" IDENT "{" { task_def | rule_def } "}" ;

task_def         = "task" IDENT "{" { op } "}" ;

op               = "move_to" TAG_NAME
                | "gather" RESOURCE_NAME INT
                | "craft" ITEM_NAME INT
                | "trade"
                | "rest"
                | "roam" INT
                ;

rule_def         = "rule" IDENT "{"
                    "when" condition
                    IDENT                 # task name, after 'do' is consumed
                    "weight" INT
                    [ "prob" FLOAT ]
                   "}" ;

condition        = clause { "and" clause } "do" ;

clause           = "hunger"  ">"  FLOAT
                | "fatigue" "<"  FLOAT
                | "season"  "==" SEASON_NAME
                | "inv" ITEM_NAME CMP INT
                | "prob" FLOAT
                ;

CMP              = ">" | "<" | ">=" | "<=" ;
```

### Important parser nuance: `do` is consumed by condition parsing

In the implementation, `parse_condition()` consumes tokens **up to and including** the `do` keyword.
That means the rule body must contain `do` and the task name must come immediately after it:

```bronze
rule hungry_work {
  when hunger > 0.25 and fatigue < 0.90 do work weight 6
}
```

---

## 4. Top-level blocks

### 4.1 `sim { ... }`

Keys:

- `seed` — integer seed for deterministic worldgen + RNG
- `days` — integer simulation length
- `cache_max` — maximum cached chunks (minimum clamped to 16)
- `snapshot_every` — integer day interval for snapshots
- `map_every` — integer day interval for ASCII maps (0 disables)

Example:

```bronze
sim {
  seed 1
  days 365
  cache_max 512
  snapshot_every 30
  map_every 0
}
```

### 4.2 `agents { count N }`

Sets the initial number of agents.

```bronze
agents { count 250 }
```

### 4.3 `settlements { count N }`

Sets the number of settlements. Settlements are placed on the world using deterministic sampling.

```bronze
settlements { count 4 }
```

### 4.4 `resources { ... }`

Each key is a per-day regeneration rate (float). Implemented keys:

- `fish_renew, grain_renew, wood_renew, clay_renew, copper_renew, tin_renew, fire_renew, plant_fiber_renew, cattle_renew, sheep_renew, pig_renew, charcoal_renew, religion_renew, nationalism_renew`

Example:

```bronze
resources {
  fish_renew 0.04
  grain_renew 0.03
  wood_renew 0.03
}
```

---

## 5. Vocations, tasks, and rules

### 5.1 `vocations { ... }`

Contains one or more `vocation` definitions.

```bronze
vocations {
  vocation farmer_crop_tender { ... }
}
```

### 5.2 `vocation NAME { ... }`

Inside a vocation you may define:

- `task NAME { ... }`
- `rule NAME { ... }`

No other constructs are allowed inside a vocation.

---

## 6. Operations (task bodies)

Operations are executed in order.

### 6.1 `move_to TAG`

Moves the agent’s activity to a terrain region described by a tag bit.

Supported tags:

- coast, beach, forest, marsh, hill, river, field, settlement

Example:

```bronze
task work {
  move_to field
  gather grain 4
}
```

### 6.2 `gather RESOURCE AMOUNT`

Adds a quantity of a resource to the agent/settlement economy. Resources must be one of:

- fish, grain, wood, clay, copper, tin, fire, plant_fiber, cattle, sheep, pig, charcoal, religion, nationalism

### 6.3 `craft ITEM AMOUNT`

Creates items. Supported items:

- fish, grain, wood, clay, copper, tin, bronze, tool, pot

### 6.4 `trade`

Triggers the built-in trade behavior.

### 6.5 `rest`

Recovers fatigue / stabilizes the agent.

### 6.6 `roam STEPS`

Random local movement / exploration for `STEPS` steps.

---

## 7. Rule conditions

Rules choose tasks based on conditions and weights.

### 7.1 Supported clauses

- `hunger > FLOAT`  
- `fatigue < FLOAT`  
- `season == spring|summer|autumn|winter`  
- `inv ITEM CMP INT` (CMP is `> < >= <=`)  
- `prob FLOAT` (0..1) — probability gate

Clauses are combined with `and`.

### 7.2 Limits

- Up to **4** `inv ...` clauses per condition (hard limit in parser).
- `hunger` and `fatigue` clauses are each single-threshold (only `>` for hunger, `<` for fatigue).

---

## 8. Practical guidance

- Keep tasks small and composable; use multiple rules to choose between them.
- Prefer tags (`move_to`) to keep resource gathering geographically grounded.
- Use `weight` for relative preference among multiple eligible rules; optionally add `prob` for stochasticity.

---

## 9. Reference: built-in names

### 9.1 Resources

- `fish` (renew key: `fish_renew`)
- `grain` (renew key: `grain_renew`)
- `wood` (renew key: `wood_renew`)
- `clay` (renew key: `clay_renew`)
- `copper` (renew key: `copper_renew`)
- `tin` (renew key: `tin_renew`)
- `fire` (renew key: `fire_renew`)
- `plant_fiber` (renew key: `plant_fiber_renew`)
- `cattle` (renew key: `cattle_renew`)
- `sheep` (renew key: `sheep_renew`)
- `pig` (renew key: `pig_renew`)
- `charcoal` (renew key: `charcoal_renew`)
- `religion` (renew key: `religion_renew`)
- `nationalism` (renew key: `nationalism_renew`)

### 9.2 Items

- `fish`
- `grain`
- `wood`
- `clay`
- `copper`
- `tin`
- `bronze`
- `tool`
- `pot`

### 9.3 Tags

- `coast`
- `beach`
- `forest`
- `marsh`
- `hill`
- `river`
- `field`
- `settlement`

### 9.4 Seasons

- `spring`, `summer`, `autumn`, `winter`
