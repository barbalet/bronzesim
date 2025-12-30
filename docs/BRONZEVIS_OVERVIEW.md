# BronzeVis

**BronzeVis** is the macOS SwiftUI visualization front-end for **BronzeSim**.

- **BronzeSim** (C): simulation core, CLI tools, DSL parser, and world/sim logic.
- **BronzeVis** (SwiftUI/macOS): interactive rendering + inspection UI that *loads and displays* BronzeSim output/assets (for example: `.bronze` scenarios and generated map/glyph views).

## Repository layout (recommended mental model)

- `src/` — BronzeSim core (C)
- `bronzesim-mac/` — macOS app(s) focused on running/hosting the sim
- `bronzevis-mac/` — macOS visualization app (SwiftUI + CoreGraphics)

## Build

Open `bronzevis-mac/bronzevis-mac.xcodeproj` in Xcode and run the **bronzevis-mac** target.

## Notes

BronzeVis is intentionally **UI-only**: it should avoid duplicating simulation logic and instead call into BronzeSim (or consume its outputs) wherever possible.
