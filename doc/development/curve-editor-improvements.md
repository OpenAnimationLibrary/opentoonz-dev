# Improve Curve Editor: assessment and stages

Assessed against OT-Dev `d850472cffdc7c502ba3dfd2ea6fd6293016b81f`
and Ztoryc `546c003b672cffda339bec796e551b3b514cfd5f` (September 30, 2026).
These enhancements use the shared Function Editor and animation code; they do
not require Ztoryc's storyboard system or scene sidecar.

## Stage 1: find and reveal channels (implemented)

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

## Stage 2: select and edit across curves (implemented in this PR)

- Shift-click keys or segments to add them across displayed curves. A rectangle
  selects keys inside it and segments crossing it, including their endpoint keys.
- Drag selected keys together in time and value. Each curve uses its own value
  scale; time movement stops together at the closest unselected neighbour.
- Alt-drag an outer endpoint of a contiguous selection to stretch several curves
  about one shared pivot and ratio. All selected curves must contain at least two
  consecutive keys. Integer-frame packing and neighbour limits apply to every
  curve, including shorter and offset selections. An ineligible selection falls
  back to ordinary dragging rather than stretching only part of the selection.
- Right-click a selected segment to apply an interpolation type to all selected
  segments in one undo operation, including selections with mixed types.
- Show selected keys/segments on every curve; use weight for the current curve
  and line patterns to distinguish the selected column from other displayed
  columns. Empty clicks clear the current curve while allowing a new rectangle.

OpenToonz's Ctrl-click key creation, Shift-drag axis constraint, and existing
single-curve editing remain available. The graph tooltip lists the gestures.
This ports graph selection/editing; tree multi-selection and its wider visibility
menu are deferred. Tangents/easing, path generation/roving, speed graphs and
curve links remain separate stages.

## Assessment and recommended follow-up PRs

| Stage | Ztoryc enhancement | Assessment and review gates |
| --- | --- | --- |
| 2 (implemented here) | Multiple curve/key/segment selection; group drag and time scaling; current-curve emphasis and per-column line styles | Useful foundation for batch editing. Port selection, hit testing, segment targeting, drag, scaling, and their undo paths together. Test different channel units, overlapping curves, frame collisions, and selection after undo. Include the later fixes for a graph with no current channel. |
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
This PR implements navigation and graph multi-selection/editing (stages 1 and 2).
It does not include tangent commands, generated paths, path-speed retiming, speed
graphs or expression linking.

## Validation

- All modified C++ translation units pass GCC C++17 syntax checks with Qt 5.15
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
- A separate focused C++ harness executes the production selection, translation,
  and stretch method bodies with lightweight parameter/setter fixtures. It covers
  explicit segment scope, mixed interpolation, invalid indices, shared translation
  limits and differing value scales, shared pivots with unequal/offset spans,
  integer packing, neighbours, left/right stretching, subrange compression, dragging
  back to the original positions and Bezier/ease handle lengths, and cleanup of
  invalid selections. The fixture does not validate the
  complete KeyframeSetter/Undo stack or UI event sequence; those require the app.
- The combined stages 1 and 2 passed the full Windows CI build and packaging
  ([run 36811131424](https://github.com/OpenAnimationLibrary/opentoonz-dev/actions/runs/36811131424)).
  A follow-up corrects the initial range of subrange stretching and restores
  Bezier/ease handle lengths when dragging back to the original range. Focused
  fixtures cover both corrections; the follow-up CI build is pending.
  macOS/Linux builds and interactive application checks remain for testing.
  In particular, verify the editor's popup and embedded
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
6. Shift-click keys/segments on different curves, and rectangle-select a mixture
   of lone keys and segments. Confirm every selected key and segment is marked.
7. Drag the group into an unselected neighbour; all selected curves should stop
   together. Check differently scaled channels and undo/redo.
8. Alt-drag both outer ends of selections with equal and unequal frame spans.
   Check integer packing, untouched neighbours, tangent lengths, return to the
   starting position, and undo/redo. A gapped or single-key selection must not
   partially stretch other selected curves.
9. Right-click a selected segment on a different/overlapping curve and choose an
   interpolation type. Only explicit selected segments should change in one undo.
10. Click empty graph space, rectangle-select again, and open the menu without a
    current curve. Confirm subsequent selection/menus still work.
11. Verify Ctrl+Shift+F and both Escape actions, ordinary text editing in search,
   and graph/spreadsheet/popup editor modes.

## Credits

Requested co-authors: MotionMonster (Jeremy Bullock), manongjohn, and
matitanimata (Franco Bianco). The source enhancements build on their shared
Tahoma2D/Function Editor work. The referenced Ztoryc commits also credit Claude
Opus 5; that attribution is retained in the port commit.
