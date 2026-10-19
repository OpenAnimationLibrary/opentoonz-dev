# Cell/key drag regressions (#6923)

Run `python3 toonz/sources/toonz/tests/test_drag_state.py` with a C++17 compiler
(`CXX=clang++` also works). The runner compiles current, unmodified production
function bodies against the explicit lightweight doubles in `*_fixture.cpp`.
It tests empty-buffer identity preservation, occupied targets, same/different
columns, forward/backward key overlap and copy, multi-column key selection,
repeated previews/return, ordinary move/insert controls, both cell orientations,
cell Undo/Redo and disposal after valid/invalid initial copy previews.
These tests do not run Qt widgets or the actual Plastic model.

For real storage/rig coverage configure the application with
`-DBUILD_XSHEET_TESTS=ON`, build `xsheet_empty_cells_test`, then run
`ctest --output-on-failure -R '^xsheet_empty_cells$'` (with the usual build
configuration and runtime library paths on Windows). The Windows PR workflow
runs this target before the full application build. It uses real TXsheet,
TStageObject and Plastic objects: 16 combinations of ordinary/mesh, partial/all
exposures, occupied/empty targets and same/different columns. It checks column,
stage-object and rig identity, two-vertex topology, child parenting, unrelated
keys, center/offset and a real stage-object stream round trip, plus locked/absent
columns and deliberate nonempty type conversion.

The native test invokes the actual storage operations used by non-insert cell
drag/Undo; it does not instantiate the GUI drag tool or claim a complete .tnz
save/reopen test. Stage stream persistence is narrower than scene persistence.

Manual integration checks still required on a full application:

- Xsheet and horizontal Timeline; ordinary and Plastic columns; partial and all
  exposures; occupied and empty targets; cells-only versus combined selection.
- Alt and Ctrl+Alt, both overlap directions, multiple columns, repeated previews,
  return to origin, supported cancellation, Undo/Redo, and save/reopen a .tnz.
- Both cell-drag preference modes when moving across columns; assert parent/child
  and rig state when the existing column-transfer machinery intentionally moves
  columns. The source-extracted cross-column probe uses cells-only behavior.
- Shift/insert and copy+insert controls. A separate pre-existing multi-key insert
  overlap issue (earlier shifts change later source rows) is not repaired by this
  overwrite patch and is not covered by the single-key insert control.

The original issue video was not accessible in the investigation environment;
these tests should not be described as replaying its exact gesture sequence.
