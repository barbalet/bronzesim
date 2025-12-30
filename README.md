# BRONZESIM

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

## SimWar - Graphics Rendering and Visual Representation

SimWar uses a **top-down, orthographic 2D rendering model** to visualize the simulation state in real time. The graphics are not a first-person or ground-level view; instead, they represent the simulated world as a **map-like overview**, where geography, settlements, and agents are all shown from directly above.

The rendering system is intentionally simple, deterministic, and tightly coupled to the simulation data structures. This allows the visual output to act as a **direct diagnostic view of the simulation state**, rather than a stylized or abstracted presentation.

---

## Overall Viewpoint

- **Camera**: Fixed, top-down (orthographic)
- **World Representation**: Rectangular grid of tiles
- **Scale**: Automatically computed so the entire world fits within the framebuffer
- **Perspective**: No depth, no horizon, no vanishing point

Each rendered frame is a snapshot of the current simulation tick.

---

## Rendering Pipeline Overview

1. Simulation updates the authoritative world state.
2. The renderer reads terrain, settlement, and agent data.
3. A CPU-based framebuffer is populated pixel-by-pixel.
4. The framebuffer is displayed via the host UI layer (e.g. CoreGraphics).

The pipeline contains no GPU shaders or 3D transforms. Rendering is fully deterministic.

---

## Framebuffer and Coordinate Mapping

- A fixed-size framebuffer (for example, 1024×800 pixels) is allocated.
- A tile size is calculated so the entire world grid fits on screen.
- Each world coordinate maps directly to a screen-space rectangle:

```
screen_x = world_x * tile_pixel_size
screen_y = world_y * tile_pixel_size
```

This guarantees pixel-perfect alignment between simulation logic and visuals.

---

## Rendered Elements

Rendering is layered in a strict order to maintain clarity.

### Background
A solid background color clears the framebuffer and establishes contrast.

### Geography Tiles
Each world cell is rendered as a colored tile. Tile color is determined by bit-flag tags representing features such as:

- Coast / water
- Forest
- Fields
- Clay pits
- Mines
- Fire / industrial activity

When multiple tags are present, a priority system determines the visible feature.

### Settlements
Settlements are drawn as high-contrast blocks layered above terrain. They act as fixed landmarks indicating long-term human organization.

### Agents (Bronze Age People)
Agents are rendered as small colored markers placed directly on the map.

- Position maps exactly to world coordinates
- Color encodes vocation or role
- Movement reflects real-time simulation decisions

This layer allows population flow, migration, and clustering to be observed directly.

### HUD and Indicators
Minimal overlay elements may indicate simulation time or tick progression.

---

## Real-Time Behavior

Each frame represents the current authoritative simulation state.

- No interpolation or prediction
- No animation smoothing
- Visuals change only when the simulation changes

This makes cause-and-effect relationships easy to reason about.

---

## Graphics Architecture (Contributor Notes)

The renderer intentionally mirrors the simulation data model:

- World grid → tile loop
- Settlements → structure overlay
- Agents → point or icon overlay

This makes the renderer ideal for debugging, experimentation, and scientific-style observation.


