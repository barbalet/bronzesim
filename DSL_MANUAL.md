# BRONZESIM DSL Manual (Language Reference)

This document is the **formal language reference** for the BRONZESIM domain-specific language (DSL).
The DSL defines Bronze Age scenarios (world settings, registries, vocations, tasks, and rules) and is
compiled at runtime by the BRONZESIM parser.

A key design goal is that **kinds are dynamic**: resources, items, and related “kind” categories are
declared in DSL files (e.g., `example.Bronze`) rather than being hard-coded as C enums.

---

## 1. Files and responsibilities

- `src/brz_parser.c`  
  Tokenizes and parses the DSL file into an in-memory configuration.

- `src/brz_dsl.c`  
  DSL helper layer and **authoritative grammar block** (used to auto-generate parts of this manual).

- `src/brz_kinds.c/.h`  
  Runtime registries for **dynamic kinds**.

- `src/brz_world.c/.h`, `src/brz_sim.c/.h`  
  Simulation structures and execution.

- `example.Bronze`  
  A complete DSL scenario (recommended starting point).

---

## 2. Lexical structure

### 2.1 Whitespace

Whitespace separates tokens and is otherwise insignificant. Newlines do not terminate statements.

### 2.2 Comments

The lexer accepts:

- Line comments: `// comment to end of line`
- Block comments: `/* comment */`

### 2.3 Identifiers

Identifiers name kinds, resources, items, vocations, tasks, rules, and variables.

**Shape:**

- Starts with a letter or underscore
- Followed by letters, digits, or underscores
- Case-sensitive

Examples:

- `grain`
- `plant_fiber`
- `bronze_axe`
- `CoastalVillage` (allowed; convention is lower_snake_case for data)

### 2.4 Literals

- Integers: `0`, `12`, `-3`
- Floats: `0.25`, `1.0`, `-2.5`
- Strings: `"Farmer"`, `"South Coast"`

---

## 3. Grammar (formal)

This grammar is **extracted from `src/brz_dsl.c`** and injected here automatically by:

- `tools/extract_dsl_grammar.py`
- `tools/update_docs.py`

To regenerate, run:

```sh
make -C src docs
```

<!-- AUTO-GENERATED-GRAMMAR-BEGIN -->

```ebnf
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
```

<!-- AUTO-GENERATED-GRAMMAR-END -->

### 3.1 Grammar notes

- The grammar is presented in an EBNF-like notation.
- Some productions include “compatibility blocks” (e.g., `sim {}` / `agents {}`) to keep older
  examples working while the DSL evolves. Prefer the newer registries (`kinds`, `resources`, `items`, `vocations`)
  for modern scenarios.

---

## 4. Semantic model

### 4.1 Dynamic kinds

The DSL defines **kind namespaces** that are mapped to internal IDs at runtime.

Example:

```bronze
kinds {
    resource;
    item;
}
```

Once registered, resources and items can reference these kinds by name:

```bronze
resources {
    grain : resource;
    fish  : resource;
}

items {
    bronze_axe : item;
}
```

**Rules:**

- A kind must be declared before it is referenced.
- Names are unique within their registry (duplicate definitions are errors).

### 4.2 Registries and resolution

The parser creates registries for:

- kinds
- resources
- items
- vocations (and their tasks/rules)

Name resolution is performed within the appropriate namespace:

- `grain` used inside a `resources {}` block resolves as a resource.
- `harvest` used inside a `vocation` resolves as a task (or rule) depending on context.

---

## 5. Top-level blocks

A DSL file consists of zero or more **top-level blocks**. Most scenarios include:

- `world { ... }`
- `kinds { ... }`
- `resources { ... }`
- `items { ... }`
- `vocations { ... }`

### 5.1 `world { ... }`

Declares scenario-level settings. Typical fields include:

```bronze
world {
    seed 12345;
    years 50;
    population 120;
    terrain coast;
}
```

**Rules:**

- Unknown keys may be accepted as generic key/value fields depending on build configuration.
- If a key is required by the simulator and is missing, the simulator should either supply a default or error out.

### 5.2 `kinds { ... }`

Declares dynamic kinds:

```bronze
kinds {
    resource;
    item;
}
```

### 5.3 `resources { ... }`

Declares named resources:

```bronze
resources {
    grain : resource;
    wood  : resource;
    clay  : resource;
}
```

### 5.4 `items { ... }`

Declares named items:

```bronze
items {
    bronze_axe : item;
    pottery    : item;
}
```

### 5.5 `vocations { ... }`

Declares vocation scripts (occupations). Each vocation contains tasks and rules.

```bronze
vocations {
    vocation farmer {
        task harvest {
            gather grain 2;
        }

        rule daily {
            do harvest;
        }
    }
}
```

---

## 6. Tasks, rules, and execution

### 6.1 Tasks

A `task` is a sequence of statements. Tasks are invoked from rules (or from other tasks, if supported).

Example:

```bronze
task harvest {
    gather grain 2;
}
```

#### 6.1.1 Statement forms (engine core)

The simulator currently supports a small core, such as:

- `gather <resource> <amount>;`
- `craft <item> <amount>;`
- `trade <resource_or_item> <amount>;`
- `do <task>;` (invoke another task)
- `chance <probability> { ... }` (probabilistic block)
- `when <condition> { ... }` (conditional block)

> The exact statement set is defined by the C engine; if you extend the engine, update the grammar block
> in `src/brz_dsl.c` so the docs update automatically.

### 6.2 Rules

A `rule` selects tasks to run, optionally gated by conditions.

Example:

```bronze
rule hungry {
    when grain < 2 {
        do harvest;
    }
}
```

### 6.3 Simulation tick model (informal)

A typical tick looks like:

1. Evaluate vocation rules in definition order
2. When a rule triggers, run its selected tasks
3. Task statements modify world state (resources/items/etc.)
4. Errors may abort the current task or rule (see error modes)

---

## 7. Error modes and diagnostics

The parser and engine produce different classes of errors. If you add new diagnostics, keep them
consistent and actionable.

### 7.1 Syntax errors (parser)

**Definition:** the input cannot be tokenized or parsed according to grammar.

Examples:

- Missing `;`
- Unexpected token
- Unterminated string
- Mismatched braces

Recommended message format:

```
SyntaxError: <file>:<line>:<col>: <message> (saw '<token>', expected '<expected>')
```

### 7.2 Semantic errors (configuration)

**Definition:** the file parses, but definitions are invalid.

Examples:

- Duplicate resource name
- Resource references unknown kind
- Task references an unknown resource
- Rule references an unknown task

Recommended message format:

```
SemanticError: <file>:<line>:<col>: <message>
```

### 7.3 Runtime warnings (simulation)

**Definition:** the configuration is valid but execution hits a recoverable issue.

Examples:

- Attempt to gather from a depleted pool
- Attempt to craft without required inputs (if the engine models recipes)
- A rule results in no executable tasks

Recommended message format:

```
Warning: <context>: <message>
```

### 7.4 Fatal runtime errors

**Definition:** continuing would produce corrupt state.

Examples:

- Out-of-bounds registry access
- Missing mandatory world fields without defaults

Recommended message format:

```
Fatal: <context>: <message>
```

---

## 8. Complete examples

### 8.1 Minimal scenario

```bronze
world {
    seed 1;
    years 10;
    population 30;
    terrain coast;
}

kinds {
    resource;
    item;
}

resources {
    grain : resource;
    fish  : resource;
}

items {
    bronze_axe : item;
}

vocations {
    vocation fisher {
        task net_fish {
            gather fish 2;
        }
        rule daily {
            do net_fish;
        }
    }
}
```

### 8.2 Example error: unknown kind

```bronze
resources {
    grain : food;
}
```

Expected diagnostic:

```
SemanticError: grain uses undefined kind 'food'
```

---

## 9. Versioning and compatibility

- The DSL is designed to evolve without breaking old scenarios abruptly.
- When adding syntax:
  - extend the parser
  - update the grammar block in `src/brz_dsl.c`
  - regenerate docs via `make -C src docs`

---

## 10. Regenerating docs

From the repository root:

```sh
make -C src docs
```

This will:

1. Extract the authoritative grammar from `src/brz_dsl.c` into `docs/grammar.ebnf`
2. Inject that grammar into this manual between the auto-generated markers
