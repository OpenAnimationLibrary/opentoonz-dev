# OT-Dev Alpha - 2026-10-02

Unofficial OT-Dev alpha build for testing the new OTLUT image-to-LUT integration.

## Build

- Source: `main` at `58973443fa3f11464b6c42f1cb9b6b700814ea81`
- Windows portable package built and tested by the OT-Dev Windows workflow

## OTLUT integration

- Adds **OTLUT Path** under Preferences > Import/Export.
- OpenToonz looks for `otlut.exe` beside `OpenToonz.exe` first.
- Right-click a single full-color raster image/level in the Scene Browser and choose **Create 3D LUT from Image Pair...**.
- The selected image is the source/original image; OpenToonz then asks for the graded/target version.
- Generated `.cube` LUTs are saved into `stuff/library/luts`.
- Existing LUTs are not overwritten; numeric suffixes are added automatically.

## Important

`otlut.exe` is a separate utility and is not bundled into this OpenToonz package yet. For this test, place `otlut.exe` beside `OpenToonz.exe`, or set its containing folder under Preferences > Import/Export > OTLUT Path.

OTLUT project: https://github.com/OpenAnimationLibrary/otlut

This is an experimental pre-release. Back up production work before testing.
