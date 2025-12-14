# BRONZESIM (scriptable occupations)

BRONZESIM is a stand-alone **ISO C99** simulation of an Early Bronze Age–style island world driven by a small **Domain‑Specific Language (DSL)** (`.bronze` files).

The goal is to keep the simulation **data-driven** (occupations, tasks, and rules defined in DSL) while the C engine remains focused on **deterministic execution**, **chunked world storage**, and **fast iteration**.

## Highlights

- **World size:** 30 miles × 30 miles
- **Cell resolution:** 3 ft × 3 ft (52,800 × 52,800 addressable cells)
- **Memory model:** the full world is **not** stored; only **loaded chunks** exist in memory via an **LRU cache**
- **Occupations:** defined in `example.bronze` under the `vocations` block (no vocation logic hard-coded)
- **License:** MIT (see `LICENSE`)

## Repository layout

- `example.bronze` — main DSL example (config + resources + vocations + tasks + rules)
- `main.c` — CLI entry point: parse → init → run loop → output
- `brz_parser.*` — lexer + parser for the DSL (word tokens + braces)
- `brz_dsl.*` — DSL “semantic” helpers (name→enum mapping, lookup helpers)
- `brz_world.*` — deterministic world generation + seasons + tags + resource sampling
- `brz_cache.*` — chunk cache and LRU eviction (only keep active world regions in memory)
- `brz_sim.*` — simulation engine: agents, settlements, task selection, execution, snapshots/maps
- `brz_util.*` — shared utilities (hashing, clamp, assertions)

## Build

```sh
make
```

## Run

```sh
./bronzesim example.bronze
```

## Documentation

- `SPECIFICATION.md` — formal technical specification (architecture, determinism, invariants)
- `DSL_MANUAL.md` — authoritative DSL language reference (grammar + semantics + limits)
- `DOCUMENTATION.md` — deep, file-by-file and flow-by-flow explanation of the codebase
- `CONTRIBUTING.md` — how to get involved
- `CODE_OF_CONDUCT.md` — community standards

## Contributing

Pull requests are welcome. See `CONTRIBUTING.md`.
