# OT-Dev Alpha - 2026-10-03

Corrected OTLUT integration alpha.

## Build

- Source: `main` at `d3a9e5c802eef1f014a71533e78bf1f1c75ee6e3`
- Includes merged PR #224
- Windows portable package built and tested by the OT-Dev Windows workflow

## OTLUT integration fix

- Adds **Create 3D LUT from Image Pair...** to the actual standard **File Browser** right-click menu.
- The previous alpha placed the action in the wrong browser class.
- Works for a single full-color raster image or raster level selection.
- Raster level selections are resolved to a real frame before OTLUT is invoked.
- OTLUT is discovered beside `OpenToonz.exe` first, then via Preferences > Import/Export > OTLUT Path.
- Generated `.cube` files are saved into `stuff/library/luts`.
- Existing LUTs are not overwritten; numeric suffixes are added automatically.

## Important

This alpha supersedes `otdev-alpha-2026-10-02` for OTLUT menu testing.

`otlut.exe` remains a separate utility. Place it beside `OpenToonz.exe` or configure its containing folder in Preferences > Import/Export > OTLUT Path.

This is an experimental pre-release.
