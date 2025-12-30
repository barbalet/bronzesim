# Changelog

## v2.5 (2025-12-30)
- Unify sources: remove duplicate `bronzevis-mac/bronzevis-mac/bronzesim/src` copy.
- Bronzevis Xcode project now compiles shared simulation C sources directly from top-level `src/` (via explicit PBX file references), excluding `main.c`.
- Cleaned build artifacts from `src/` (no *.o or binaries in repo).
