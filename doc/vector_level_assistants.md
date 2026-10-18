# Vector level assistants

With the Brush Tool's **Assistants** option enabled, a Toonz Vector, Toonz Raster,
or Raster drawing can use ordinary vector strokes in its immediately neighboring
Xsheet columns as guides. Use the drawing exposed on the current scene frame.
Reference columns must be visible in the camera stand; their Preview visibility
can be off. This operates in scene editing mode.

Enabled assistant guides and active replicators retain priority. If neither is
available, the brush checks the previous and next column for vector guides near
the start of a stroke. With no eligible reference, drawing behaves as before.

Start within 32 destination drawing units of a vector centerline (pixels for
raster levels, native drawing units for vector levels). The initial movement
helps select the intended curve. Its identity is retained until the pen lifts;
intersections and nearby curves do not switch the reference.
Attraction is strong (90%) near the curve and eases in over the start of a stroke.
It fades with distance, and pulling out of range releases the guide for the rest
of that stroke. Open ends permit continued movement. Closed curves can be
followed repeatedly in either direction. The selected curve is highlighted while
drawing. Brush pressure, tilt, timing, color, and texture still come from the
normal brush pipeline.

The feature adjusts new brush input before pixels are painted or a vector stroke
is created. Vector output retains the usual pressure-based thickness, styles,
Accuracy, and Smooth settings. It uses no new level type or stored scene data.
Column placements and destination DPI are accounted for when taking a per-stroke
snapshot of the reference; vector levels use their native drawing coordinates.

## Validation

The standalone production-geometry regression test covers initial direction,
lateral wobble, crossings within one vector, closed-loop seams and repeated laps,
reverse drawing, release, open ends, sparse input, replay, quadratic approximation,
and degenerate geometry:

```sh
g++ -std=c++17 -DLINUX -I toonz/sources/include \
  toonz/sources/tnztools/tests/vectorassistants_test.cpp \
  -o /tmp/vectorassistants_test
/tmp/vectorassistants_test
```

Interactive checks for the Windows build:

- Repeat on TLV and full-color raster levels with standard, Pencil, and MyPaint
  brushes, including pressure-sensitive strokes and undo/redo.
- Repeat on PLI levels with pressure-based thickness and different Accuracy and
  Smooth settings. Confirm the result remains an editable vector stroke.
- Compare Assistants off/on; no reference, empty cells, hidden reference columns,
  and ordinary assistant/replicator levels should retain their expected behavior.
- Try crossing and closely parallel vector strokes, small loops, corners, open
  ends, and pulling away toward another curve without lifting the pen.
- Try translated, rotated, scaled, and parented columns; different raster DPI and
  viewer zoom; held exposures and frame changes; and brush smoothing enabled.

AI assistance: implemented with OpenAI Codex from Rodney Baker's request to use
adjacent vector levels as smooth drawing guides through the existing
Assistants option, without jumping between reference lines.
