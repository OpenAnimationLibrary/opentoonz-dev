# Trail Cycle in Style Editor Settings

## Purpose and provenance

Adapt the Trail Cycle part of opentoonz/opentoonz#7085, head
`2dc7a2ab5a4eda2912698a56c6b33cd9d0403754`, by doangelshavewings.
The new control belongs to the selected Trail style in **Style Editor > Settings**,
next to the existing Distance and Rotation controls. It is not a global Brush
preference. There is no additional permanent toolbar control in this revision.

Upstream source: https://github.com/opentoonz/opentoonz/pull/7085

Retained/adapted: gesture selection and commitment, source-frame iteration,
per-stroke offset/step, PLI outline extension and safety checks, and PLI/state
regression tests. New: per-style enum, Settings integration, palette/PLI style
metadata, copy support and a non-animated drawing-default policy.

Not ported: artwork-bound raster cropping/sizing, texture cache/quality changes,
bounding-box changes, toolbar cycle property and its global preference, shortcut
registration, or upstream CI changes. Existing Raster Trail loading, sizing,
spacing, texture preparation and bounding boxes remain unchanged. In particular,
heterogeneous source canvases retain the legacy common transformation convention.

## Behavior

- **Off** (default): normal source sequence on each new stroke; no shared cursor
  advancement between gestures. Off is not a frozen source-frame mode.
- **Forward**: advance between committed gestures; sequence forwards along the
  stroke. With a fresh three-frame resource, click-stamps use 1, 2, 3, 1.
- **Backward**: sequence backwards; fresh click-stamps use 3, 2, 1, 3.
- **Repeat**: hold the shared selection and repeat that one source frame along the
  entire stroke. A fresh cursor starts at the first source frame.

Changing Forward/Backward/Repeat preserves the shared cursor. Canceled gestures,
ordinary styles, Off, single-frame sources and Frame Range drawing do not advance
or replace it. The next committed different Trail resource, palette, style slot
or frame count resets the cursor. This intentionally retains upstream's one-last-
cursor policy, not a separately remembered counter for every palette slot.
Assistant/symmetry replicas share one selection and advance once per gesture.

The Settings value is editable independently of the active tool. Frame Range
ignores it at drawing time but does not erase it. A missing or single-frame source
shows the control disabled with an explanation and retains its saved mode.

Settings uses the existing Auto/Apply and style undo path. The Brush reads the
applied palette style at gesture start. Changing the mode does not change already
committed strokes. Distance and Rotation remain live appearance parameters.
Sequence position belongs to the drawing session, never to a renderable style:
painting does not mutate palette settings; previews/icons/rendering do not consume
steps. Undo/redo of a stroke restores its recorded metadata, not the tool cursor.

## Click to stamp

With a loaded Trail style and Frame Range off, a stationary Brush click now
creates one stamp without a drag. This also works with Cycle Off and single-frame
sources. The click uses the current brush thickness and style Rotation; a
stationary tablet tap retains its peak sampled stroke thickness rather than a
zero-pressure release. Zero-width strokes are not enlarged into visible marks.

At gesture completion, only point-like Trail strokes are given a real, open
carrier: two straight quadratic chunks, one stage unit long, starting at the
click and pointing along +X. Both renderers have a minimum stamp interval of two
stage units, so this supplies exactly one placement even with negative Distance.
The carrier has enough extent to survive PLI coordinate quantization. Normal
drags (including tiny ones), motion paths, Frame Range, and ordinary brush dots
are unchanged. Self-snapping must not close the synthesized carrier. Assistant
replicas pass through the same completion and undo path and still share one cycle
selection. There is no new toolbar or Settings toggle.

Single vector stamps bypass the thin-line preview approximation, whose short
line strip would otherwise have no vertices. Long Trail strokes retain that
optimization. This also restores previously invisible one-placement vector Trails
at small display sizes; it does not change Raster Trail sizing or cropping.

## Persistence and compatibility

The mode is an enum on both Trail style classes. Copy/clone preserve it. It is a
non-interpolated drawing default: palette frame scrubbing keeps it while animating
existing appearance parameters. The additional parameter is index 2; Distance and
Rotation retain indices 0 and 1.

TPL/scene/studio palette style records store nondefault modes in an optional
`trailCycle` attribute, including keyframe records. Their old style payloads and
style IDs are unchanged. Do not append a raw value to the unversioned TPL payload:
the old reader requires its end tag immediately after the known fields.

PLI's separately bounded, typed style record stores nondefault modes as an
optional `trail-cycle-v1` string followed by an integer. Its old reader consumes
the known style fields and ignores remaining record parameters. Missing metadata
means Off. Invalid modes default to Off. These are source-level compatibility
claims, not a completed released-application downgrade round trip. Resaving in an
old application loses new style defaults and stroke cycle metadata.

The upstream PLI outline extension stores each stroke's realized offset/step.
Default strokes retain offset 0, step 1. The extension supports reading files
written by upstream #7085 without requiring the mode to be on a toolbar.
`TStroke::OutlineOptions` and the Trail classes change native layout; rebuild all
native dependent modules against matching headers/libraries.

No existing padded source is implicitly converted to artwork-bound sizing.
Changing a Trail resource still changes artwork that references that resource,
as it did before. The preservation promise concerns changing the cycle default,
not freezing resource pixels or all live style appearance settings.

## Automated checks

Dependency-light, compiled gesture-state and click-carrier geometry tests:

```sh
cmake -S toonz/tests/trail_settings -B build/trail-settings
cmake --build build/trail-settings
ctest --test-dir build/trail-settings --output-on-failure
```

The focused PR workflow runs those tests. They are not application or OpenGL coverage.
The existing OT-Dev Windows/macOS PR workflows remain unchanged.

After building matching `tnzcore`, enable actual palette/PLI tests, for example:

```sh
cmake -S toonz/tests/trail_settings -B build/trail-settings \
  -DCMAKE_BUILD_TYPE=Release \
  -DTNZCORE_LIBRARY=/absolute/path/to/toonz/build/lib/opentoonz/libtnzcore.so
cmake --build build/trail-settings
ctest --test-dir build/trail-settings --output-on-failure
```

Use the corresponding `.dylib` on macOS or import `.lib` on Windows and make the
matching runtime libraries discoverable. Tests and core MUST match Debug/Release
because `TSmartObject` layout depends on `NDEBUG`. Qt 5 must be discoverable. Core-
backed tests cover enum/defaults, copy/clone, TPL and PLI style persistence,
non-animated modes, stroke sequence isolation, malformed/truncated outline
payloads, signed-magnitude limits and stroke copy/split/transform retention.

## OT-Dev application acceptance checklist (not yet claimed complete)

- [ ] Build the new PR on Windows/macOS; perform Linux rendering checks.
- [ ] Both raster-source and vector-source Trail Settings show all four modes.
- [ ] Click without dragging stamps once with raster/vector Trails, including Off
      and single-frame sources. Test several Size, Rotation and Distance values.
- [ ] Tablet tap/release, snapping, zoom and assistant replicas preserve placement
      and advance the cycle once per gesture. A click makes one undoable gesture.
- [ ] Save/reopen click stamps; verify nonzero carrier length and the same frame.
- [ ] Ordinary dots, short drags, loops and Frame Range retain existing behavior.
- [ ] Auto off: unapplied changes do not affect drawing; Apply changes new strokes.
- [ ] Auto on and style undo/redo work; two palette slots keep independent modes.
- [ ] Mode changes do not resequence old strokes, including after save/reopen.
- [ ] Frame Range modes still select normally; Frame Range drawing ignores cycle.
- [ ] Missing/single-frame sources disable the field without clearing the mode.
- [ ] Mode switching, cancellation and assistant/symmetry replicas work as above.
- [ ] Style copies, palette copies and Studio Palette transfers retain the mode.
- [ ] Animated Distance/Rotation work; scrubbing never replaces the cycle default.
- [ ] Viewer, Preview, final output and column icons show the same saved sequence.
- [ ] Old padded-source scenes render as before; no artwork-bound resizing occurs.
- [ ] Released older version: open/save disposable TPL/PLI copies, then reopen here.
- [ ] Scene switching and stroke undo/redo do not dereference obsolete palettes.

## AI assistance

ChatGPT (GPT-6 Astra Pro; exact snapshot unavailable) assisted this adaptation,
source review, implementation, tests and documentation. User direction: adapt
#7085 into a new OT-Dev PR with Trail Cycle owned by the style and edited in the
Settings tab; preserve completed strokes and test the application in OT-Dev.
This work is experimental and does not constitute human review or upstream
merge approval. Original implementation credit remains with doangelshavewings.
