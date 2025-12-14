# BRONZESIM (scriptable occupations)

A stand-alone C simulation of an Early Bronze Age–style island world:

- **World size:** 30 miles × 30 miles
- **Cell resolution:** 3 ft × 3 ft (52,800 × 52,800 addressable cells)
- **Memory model:** the full world is **not** stored; only **loaded chunks** exist in memory via an LRU cache.
- **Occupations:** 100% **data-driven** via `example.bronze` (no vocation logic hard-coded in C)

## Build

```bash
make
```

## Run

```bash
./bronzesim example.bronze
```

During the run it prints a summary every 10 days.

### Snapshots

If `snapshot_every N` is set in `sim { ... }`, the simulator writes:

- `snapshot_day00030.json`
- `snapshot_day00060.json`
- ...

### ASCII maps (optional)

If `map_every N` is set, it writes coarse ASCII maps:

- `map_day00030.txt` etc.

## DSL Quick Reference

Top-level blocks:

- `world { seed <u32> }`
- `sim { days <int> cache_max <int> snapshot_every <int> map_every <int> }`
- `agents { count <int> }`
- `settlements { count <int> }`
- `resources { fish_renew <f> grain_renew <f> wood_renew <f> clay_renew <f> copper_renew <f> tin_renew <f> fire_renew <f> plant_fiber_renew <f> cattle_renew <f> sheep_renew <f> pig_renew <f> charcoal_renew <f> religion_renew <f> nationalism_renew <f> }`
- `vocations { ... }`

### vocations

```text
vocations {
  vocation <name> {
    task <taskName> { <op> ... }
    rule <ruleName> { when <conditions> do <taskName> weight <int> [prob <0..1>] }
  }
}
```

### ops

- `move_to <tag>` where tag is one of:
  - `coast beach forest marsh hill river field settlement`
- `gather <resource> <amount>`
  - resources: `fish grain wood clay copper tin`
- `craft <item> <amount>`
  - items: `pot bronze tool`
- `trade`
- `rest`
- `roam <steps>`

### conditions (AND)

- `hunger > <float>`
- `fatigue < <float>`
- `season == spring|summer|autumn|winter`
- `inv <item> <cmp> <int>`
  - cmp: `> < >= <=`
  - items: `fish grain wood clay copper tin bronze tool pot`
- `prob <float>` (may appear as a condition or at end of rule)

Example:

```text
rule winter_wood {
  when season == winter and fatigue < 0.85
  do woodcut
  weight 5
}
```

## Emergent role switching / apprenticeship

- Teen agents (10–16) can occasionally inherit their household parent's vocation (apprenticeship).
- Every ~30 days, the sim measures per-capita shortages and nudges a small number of adults into deficit vocations
  (if vocations named `farmer`, `fisher`, `potter`, `smith` exist).
