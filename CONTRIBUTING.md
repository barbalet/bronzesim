# Contributing to BRONZESIM

Thanks for your interest in BRONZESIM. This project is intentionally small, explicit, and deterministic.

## Development principles

- **Clarity over cleverness**
- **Determinism over convenience**
- **Data-driven behavior**: add occupations/rules in DSL first
- **Bounded memory**: do not introduce unbounded allocations in hot paths

## What contributions are welcome

- DSL extensions (new operations, new condition clauses)
- New example `.bronze` scenarios (new worlds, new vocation sets)
- Better error messages (without weakening invariants)
- Performance improvements in cache / stepping
- Documentation improvements

## Coding standards

- ISO C99
- No external dependencies
- Prefer small functions with explicit inputs/outputs
- Use `BRZ_ASSERT` for invariants
- Keep modules focused; avoid “utility dumping grounds”

## Pull request checklist

- [ ] Code builds with `make`
- [ ] Changes are documented (README / DSL_MANUAL / SPECIFICATION as needed)
- [ ] Any new DSL feature includes an example in `example.bronze` (or a new example file)
- [ ] Avoid breaking determinism unless explicitly justified

## How to propose larger changes

Open an issue describing:
- The problem
- The minimal change needed
- How determinism and memory bounds are preserved
