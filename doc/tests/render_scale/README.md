# Signed render Shrink

Preview Settings and Output Settings accept signed Shrink values. Positive values
retain their existing reduction behavior. Negative values multiply both image
dimensions by their absolute value; -1 is native resolution. Zero resets to 1.
For a 1920 x 1080 camera, 2 produces 960 x 540, -2 produces 3840 x 2160,
and -3 produces 5760 x 3240. Camera settings and framing do not change.

The scene's existing integer setting stores the sign, independently for Preview
and Output. Task rendering and tcomposer use the same convention, including
`-shrink -2`. Older applications will not understand negative values; restore
Shrink to 1 before using the scene in an older build.

## Image quality

The FX tree renders at the enlarged resolution. Vector levels and procedural FX
are evaluated at that resolution; raster inputs use the existing Resample Balance
selection. Start with **Lanczos window, rad = 3** for raster enlargement, then
compare with Mitchell or High for the particular artwork. Existing filter choices
are preserved. Preview has its own filter setting.

No FFmpeg post-render resize or AI upscaler is introduced. A future AI option can
be a separate output processing stage. Doubling each dimension makes four times
as many pixels, so rendering and cache memory requirements increase accordingly.

## Automated checks

The standalone test compiles the production scaling helper with OpenToonz's actual
geometry implementation. It needs a C++17 compiler and CMake, without Qt:

```
cmake -S doc/tests/render_scale -B build-render-scale
cmake --build build-render-scale
ctest --test-dir build-render-scale --output-on-failure
```

It checks native, zero, positive and negative values; the fixed camera origin;
off-center subcamera coordinates; non-mirrored transforms; odd resolutions;
inclusive subcamera edges; and dimension overflow.

## Application checks before merge

- Use vector, Toonz Raster and RGBA raster levels with a procedural FX and
  camera animation. Compare Shrink 1 and -2: composition must match and a saved
  PNG must have exactly twice the width and height. Check -3 and an odd-sized
  camera as well. Compare Lanczos 3 with the other resampling filters.
- Preview the full camera and an off-center subcamera in both the Main Viewer
  (Apply Shrink to Main Viewer enabled/disabled) and FX Preview. Pan, zoom,
  change frames, and switch 2 / -2 / 1: no stale pixels, clipping or mirroring.
- Check the FX histogram in camera view after changing Preview Shrink.
- Render a sequence, a movie, stereo outputs, multimedia output and a
  clapperboard. The clapperboard must match the output size with readable text.
  Open the result in Flipbook and confirm signed render values never reach
  image-reader shrink settings.
- Save/reload independent negative Preview and Output settings. Add a task,
  edit its Shrink, and render it via tcomposer; compare with the interactive PNG.
- Exercise rendered Animated SVG export and confirm asset dimensions and framing.
- Retest positive Shrink values to confirm the existing reduction behavior.
