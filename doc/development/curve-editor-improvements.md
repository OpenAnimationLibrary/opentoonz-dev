# Improve Curve Editor: assessment and stages

Assessed against OT-Dev `d850472cffdc7c502ba3dfd2ea6fd6293016b81f`
and Ztoryc `546c003b672cffda339bec796e551b3b514cfd5f` (September 30, 2026).
These enhancements use the shared Function Editor and animation code; they do
not require Ztoryc's storyboard system or scene sidecar.

## Stage 1: find and reveal channels (this PR)

- Search the parameter tree by channel, column, FX, or nested group name,
  ignoring case and leading/trailing spaces. Expand matching branches.
- Use **Ctrl+Shift+F** within the editor to focus search. Escape clears the
  search; another Escape releases its keyboard focus.
- **Animated only** filters the tree across columns and effects. It combines
  with search and existing per-folder filters.
- Selecting a stage object or FX reveals its group. Selecting a graph curve
  reveals its channel and opens the necessary ancestors.
- Reapply filters after model refresh, including newly created and renamed
  objects and dynamic FX material groups.

Search and the new global checkbox affect the tree list, preserving graph
activation. Existing per-folder **Show Animated Only** retains its original
behaviour. Navigation respects filtered-out rows. OT's current graph/spreadsheet
layout, interpolation, file format, editing operations, and undo remain in use.
Filter settings are local to the open editor and are not written to scenes.

Source: Ztoryc's [navigation and selection work](https://github.com/matitanimata/ztoryc/commit/1bb8026294678f37bb059c8f78334cded5ee96e3)
and [current-column/channel synchronization](https://github.com/matitanimata/ztoryc/commit/1380b90a643cfe216be21b9c97f3bc99c75cf5de),
adapted to OT-Dev rather than replacing complete files.

## Assessment and recommended follow-up PRs

| Stage | Ztoryc enhancement | Assessment and review gates |
| --- | --- | --- |
| 2 | Multiple curve/key/segment selection; group drag and time scaling; current-curve emphasis and per-column line styles | Useful foundation for batch editing. Port selection, hit testing, segment targeting, drag, scaling, and their undo paths together. Test different channel units, overlapping curves, frame collisions, and selection after undo. Include the later fixes for a graph with no current channel. |
| 3 | Auto Bezier, Flat, normalized tangent copy/paste, named easing presets | Good candidates once selection semantics are stable. Reuse KeyframeSetter and existing keyframe types. Include endpoint corrections; check extrema/overshoot, zero value/time spans, linked handles, adjacent segments, and undo. Treat optional automatic tangents on key creation separately because insertion also changes neighbouring keys. |
| 4 | Rove Keys / Even Speed Along Path; Generate Path from Keys; inactive path-channel indicators | Keep separate from tangent editing: roving moves whole stage-object keys, and path generation changes the object's motion source. Verify synchronized rotation/scale keys, integer-frame rounding, existing spline replacement, attach/detach, undo/redo and save/reload. Constant speed applies to path-length percentage, not arbitrary independent X/Y channels. |
| 5 | Read-only speed graph | Useful inspection aid, but Ztoryc overlays the lower value graph and samples evaluated curves on every paint. Define the derivative's meaning and units, guard non-finite expression/file values, check discontinuities and mouse/wheel interaction, and measure cost with many selected curves before porting. |
| 6 | Link Curves with delay/multiplier/offset; driver/driven tree markers | Useful expression authoring. Verify destination overwrite behaviour, expression grammar and frame numbering, nested FX/channel matching, dependency cycles including links created in the same operation, disjoint selected spans, undo and save/reload. Source implementation matches target channels by expression name and builds an interval from selected key extrema; arbitrary selections need an explicit scope policy. |

Relevant source commits:

- [No-current-channel fixes](https://github.com/matitanimata/ztoryc/commit/387d67fe787e83e8684ee5acaf9dc375d5ba1150)
- [Tangent tools and roving](https://github.com/matitanimata/ztoryc/commit/7bbd80dfe4b5b52802efce17fffad3c908cfbf7c)
- [Endpoint corrections, path generation and inactive channels](https://github.com/matitanimata/ztoryc/commit/703397712e9f44048b5d7912954d49248ba3edb3)
- [Easing presets](https://github.com/matitanimata/ztoryc/commit/07e6528f7f044d152e56e3555a4f4c3cc90b51e2)
- [Speed graph and curve linking](https://github.com/matitanimata/ztoryc/commit/9b8e73921288bea062fe191a0bad85ff20b7406a)

Do not wholesale cherry-pick the source series: it also contains Tahoma-specific
Drawing Number handling and wider crash hardening. Preserve OT-Dev's dynamic
material parameter support and upstream curve behaviour when adapting each stage.
The selected stage-1 implementation does not include multi-selection, tangent
commands, time retiming, generated paths, speed graphs or expression linking.

## Stage 1 validation

- Both modified C++ translation units pass GCC C++17 syntax checks with Qt 5.15
  headers. Qt moc generation and generated-source syntax checks cover the changed
  headers. These are compilation checks, not a complete linked OpenToonz build.
- An offscreen Qt harness uses the production TreeModel/TreeView and extracts the
  changed filtering/reveal method bodies without duplicating their algorithms.
  Lightweight channel fixtures cover case-insensitive and trimmed search, nested
  and ancestor matches, combining/clearing filters, newly added/renamed rows,
  activation preservation, legacy folder filtering, ancestor expansion, and
  navigation to hidden and null items. This is focused validation, not an
  end-to-end test of the running OpenToonz application.
- Changed C/C++ lines pass clang-format 14; `git diff --check` passes.
- Full Windows/macOS/Linux builds and interactive application checks remain for
  CI and manual testing. In particular, verify the editor's popup and embedded
  modes and dynamic GLB material channels in the built application.

Manual acceptance checks:

1. Create keyed and unkeyed channels in two columns and an FX with nested color
   parameters. Show several curves in the graph.
2. Search for a column name, a nested group name, and a channel name. Confirm
   matching branches open and that clearing search restores the list.
3. Toggle Animated only during search, then clear both filters. The displayed
   graph curves should remain the same throughout.
4. Add, delete and rename columns/FX while a filter is active; switch scenes and
   sub-xsheets. Confirm hidden rows do not remain stranded when filters clear.
5. Select a column/camera/pegbar or FX, then a different graph curve. Confirm the
   corresponding visible group/channel is revealed. A filtered-out item should
   not override the filter.
6. Verify Ctrl+Shift+F and both Escape actions, ordinary text editing in search,
   and graph/spreadsheet/popup editor modes.

## Credits

Requested co-authors: MotionMonster (Jeremy Bullock), manongjohn, and
matitanimata (Franco Bianco). The source enhancements build on their shared
Tahoma2D/Function Editor work. The referenced Ztoryc commits also credit Claude
Opus 5; that attribution is retained in the port commit.
