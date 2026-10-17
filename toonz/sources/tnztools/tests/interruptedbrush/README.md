# Interrupted brush regression checks

Run from the repository root with Python 3 and GCC (or set `CXX=clang++`):

```sh
python3 toonz/sources/tnztools/tests/interruptedbrush/raster.py .
python3 toonz/sources/tnztools/tests/interruptedbrush/fullcolor.py .
python3 toonz/sources/tnztools/tests/interruptedbrush/vector.py .
python3 toonz/sources/tnztools/tests/interruptedbrush/events.py .
```

These executable tests extract production method bodies/fragments into temporary
C++ harnesses, compile with warnings as errors and ASan/UBSan, and execute them.
They do **not** build OpenToonz or exercise Qt, real raster geometry, tablet
hardware, the TLV codec, frame creation/removal or application GUI event ordering.
The raster snapshot test models savebox cropping in memory; it is not a real
save/reopen test. Test doubles simplify ownership, pixels, bounds and caches.

Coverage:

- `raster.py`: 48 assertions for originating level/frame identity, commit and
  undo/redo bounds, both minimize-savebox preferences, unchanged destinations,
  missing origin/image/raster, empty undo tiles and repeated completion. Uses
  actual `commitPainting`, `MyPaintBrushUndo::redo` and `TRasterUndo::undo` bodies.
- `fullcolor.py`: 41 assertions using the actual `commitStroke` body, covering
  origin identity and captured pixels, missing origin/image/raster/work raster,
  empty tiles, cleanup and repeated completion. It does not exercise Full Color
  undo execution or disk serialization.
- `vector.py`: 26 assertions using the actual frame-range completion branch,
  `onImageChanged` and `resetFrameRange`. A synchronous notification double
  reproduces the callback that clears working tracks during interpolation.
  Forward/backward Ctrl-continuation to second and third endpoints is covered
  in modeled scene/level editing modes, plus ending a range and discarding an
  interrupted stroke while preserving an existing preview. Interpolation
  geometry and actual scene row/frame mapping are not implemented by the double.
- `events.py`: 21 assertions executing the modifier-sync and leftover-event guard
  fragments from all three brushes. Drag/release are suppressed until a fresh
  down; hover and modifier synchronization remain available. This tests shared
  guard logic, not device pressure, snapping, resizing or hardware input.

`--ref <commit>` loads production source from Git instead of the worktree.
On upstream #7099 head `e4f049495a1b765ce6203ba3a378e901b5bc0770`, raster
has 25 failing assertions and Vector has 8; Full Color and events pass.
With the origin savebox/redo and Vector preview corrections all 136 pass.
`raster.py --commit-only` intentionally restores the incorrect redo call and
has 10 failures. These negative controls distinguish the two necessary raster
corrections. Do not interpret expected failing controls as CI tests to enable.

## Integration and remaining manual validation

This integration preserves all six #7099 commits through
`78469a7c27e0ecb42870ad6cc7eb4e0eaa8a518e`, including the newly upstreamed
origin savebox and redo fixes, with authorship and cherry-pick provenance.
The additional brush correction retains the completed Vector endpoint preview
across synchronous image notifications. OT-Dev Ctrl+Alt resizing (5989bf2) and MyPaint erasing
(ec975d2) are retained. Exact source comparisons verified the three
`inputMouseMove` resize implementations, MyPaint brush implementation/header,
and Toonz Raster `askRead`, `askWrite`, `updateWorkAndBackupRasters` are unchanged.
That is source inspection, not runtime proof of those features.

Full local builds could not run in the preparation environment: no CMake/Qt
development setup, and attempting the documented Debian/Ubuntu dependencies
failed to acquire the dpkg lock because root privileges were unavailable.
Historical upstream CI and GUI reports are not validation of these corrections.
Use this PR's exact final SHA for build results.

Before merging, run on a built application:

1. Toonz Raster and Full Color: put A.1 and B.1 in adjacent scene rows, with
   distinct original marks; also use unequal raster dimensions. Start a stroke
   on A, hold the mouse/tablet down, arrow to B, continue dragging, release.
   Verify only A receives the stroke and undo/redo operate on A while B remains
   selected. Repeat same-level/different-frame, different-level/different-ID,
   and empty-destination cases. No leftover release should create a cell.
2. Toonz Raster: extend well beyond the previous savebox. Repeat with minimize
   savebox enabled/disabled and pencil/hard, soft and MyPaint styles. Save all,
   fully unload/restart and reopen after interruption, after undo and after
   redo; all expected outlying pixels must survive. Perform undo/redo before
   closing; undo history need not survive reopening.
3. Vector: hold and arrow during a stroke; verify the interrupted stroke is
   discarded and subsequent drag/release writes nothing. New down must work.
   With frame range enabled, draw endpoints forward and backward, hold Ctrl
   through endpoint two and three, and confirm the red preview moves to each
   latest endpoint and interpolation still works. Test scene and level editing,
   repainting the first endpoint, pointer leave/reentry, and releasing Ctrl.
4. All brushes: switch tools midstroke, activate again and draw. Check modifier
   changes while leftover events are ignored, Ctrl+Alt resizing, Shift straight
   lines, and mouse/tablet pressure behavior.
5. OT-Dev MyPaint eraser: exercise Lines, Areas and Lines & Areas modes before
   and after cross-level interruption, including undo/redo and save/reopen.
   Existing erasing behavior must remain intact.

## Concurrent PRs kept out of this integration

- OT-Dev #220 adds Toonz Raster Auto Fill/Close in the old completion path before
  deleting the tile saver. Its eventual integration must execute that work on
  the retained originating image inside `commitPainting`, before tile-saver
  deletion/undo capture; it must not reintroduce current-selection lookups.
- OT-Dev #214 changes Vector grouping/fill/undo and stroke insertion. Retain
  snapshot-based preview handling around its additional image notifications.
- OT-Dev #20 changes Vector scene-row frame ranges and Ctrl-continuation. Its
  range bookkeeping needs explicit reconciliation with the endpoint snapshot.

Read-only `git merge-tree --write-tree` checks found a content conflict with
#220 in `toonzrasterbrushtool.cpp`, multiple conflicts with #20, and a clean
textual merge with #214. Their feature changes are not part of this PR. Re-run the above
checks after resolving overlaps; a clean textual merge is not behavioral proof.
