# Skeleton Tool: bone and constraint exploration

This OT-Dev experiment adds persistent bone tips, selectable tapered bone
bodies, and an ordered constraint stack to the existing Skeleton Tool. The
starting point is Animation:Master's distinction between editing a bone and
posing it, together with target constraints, offsets, and enforcement.

The existing stage-object hierarchy and animation channels remain the source
of animation. This is a 2D rigging exploration, not a replacement for Plastic
or a complete Animation:Master rig implementation.

## Try the bones

1. Select a drawing column and the Skeleton Tool. Enable **Modern Bones** in
   Tool Options or the tool's right-click menu. Turn **Bone Names** off if
   labels overlap. The setting can be turned off to use the original display
   and controls.
2. In **Build Skeleton**, click a bone body to select its column. Drag its
   tip to set a persistent length and direction. Hold Shift for 15-degree
   direction snapping. Drag its root to adjust the existing drawing pivot.
   Ctrl-drag a body to use the existing parenting/hook attachment operation.
3. Until a tip is edited, an existing parent's first child provides its visual
   endpoint. **Bone and Constraints...** initializes its tip fields from that
   child. A leaf starts with a one-inch tip along its local Y axis.
4. Right-click **Bone and Constraints...** to name a bone, enter its tip
   numerically, and manage its constraints. The name is the existing column's
   stage-object name. Enable **Use persistent bone tip** to keep an endpoint
   independent of the current child drawing positions.
5. In **Animate**, drag a body or tip to rotate its column. Ctrl-drag to move
   it. Existing animation channels, Global Key, drawing changes, and the
   drawing browser continue to be used. In **Inverse Kinematics**, body/tip
   dragging uses the existing IK solver and pin controls.

Bone tip edits and dialog edits support Undo/Redo. Escape cancels an active
tip drag; switching tools or frames also cancels it. Tip editing does not
create animation keys. Turn off active constraints before dragging a rest tip.

## Try constraints

Select the **driven** bone, open **Bone and Constraints...**, select a type,
and click **Add Constraint**. Choose a target when the type requires one.
New constraints use the bone tip shown in the dialog. Only objects assigned
constraints acquire this behavior; children continue to inherit their parent
transforms through the existing hierarchy.

| Constraint | Result |
| --- | --- |
| Translate To | Move the bone pivot to the target point, with a world-space position offset. |
| Orient Like | Align the bone direction to the target bone direction, with an angular offset. |
| Aim At | Point the bone toward the target point, with an angular offset. |
| Parent Like | Follow target position and direction; position offsets rotate with the target. The bone retains its own scale and shear. |
| Distance Limit | Keep the pivot inside a minimum/maximum distance band around the target point. |
| Position Limit | Bound the pivot's X/Y position in the existing parent's coordinate system. |
| Rotation Limit | Bound the bone direction in parent space, within -180 to 180 degrees. |
| Scale Limit | Bound the magnitudes of the parent-space affine X/Y axes. Preserve reflection and shear direction. Values are factors, not percentages. |

Target points are **Pivot** or **Bone Tip**. The pivot includes the object's
current hook/pivot selection. Set a persistent tip on a target before using
its Bone Tip as an endpoint. A dedicated arbitrary-hook target selector is
deferred.

Orient Like and Parent Like also need a persistent tip on a target drawing
column, so its evaluated bone direction matches the displayed bone. A pegbar,
table, or camera target uses its local Y direction.

Use **Capture Current Offset** before accepting a new Translate To, Orient
Like, Aim At, or Parent Like constraint when the currently visible pose should
be maintained. Capture reads the pose currently in the viewer. The dialog
commits with OK; it does not preview its uncommitted edits in the viewer.

Constraints evaluate from top to bottom. Move them up/down to change order;
for example, Translate To followed by Aim At aims from the translated
position. Multiple constraints blend sequentially. Putting limits last makes
them bound the preceding constraint results. Limits also apply to poses
produced by the Animate Tool; they do not rewrite the authored channels.

**Enforcement** is a percentage. An unkeyed value is constant. **Set Key**
creates a Hold key at the current frame. Once keys exist, changing enforcement
adds or updates a key at that frame. **Remove Key** removes its current-frame
key. The base enforcement applies before the first key. Start/End frame
limits are inclusive; **All** removes the corresponding boundary. Enforcement
keys are edited here in this first pass, not in the Function Editor.

## Evaluation and scene behavior

- Constraints change evaluated placement, so the viewer and render paths read
  the same result. They do not bake or overwrite X/Y/rotation/scale keys.
  No previous-frame simulation state is required.
- Target identities are UUIDs, rather than column indices. Renaming,
  inserting, deleting, or reordering columns cannot redirect an existing link
  to an unrelated drawing. A missing target stays recorded and inactive.
  Restoring the deleted object restores its identity and link.
- Copying a group of stage objects remaps its internal target identities to
  the copied objects. External links retain their existing identities; a
  target omitted from a copy into another xsheet remains unresolved.
- Assignment and reparenting check the combined parent/constraint dependency
  graph. Disabled constraints and inactive frame ranges also count, so
  re-enabling them cannot introduce a latent cycle. Cyclic links loaded from
  malformed scene data are reported and skipped without recursive solving.
- Coincident aim points and degenerate transforms are skipped safely. A
  coincident Distance Limit uses the bone direction as a deterministic
  separation direction. Finite values, ordered bounds, tip length, and stack
  sizes are validated when reading new rig data.
- Legacy pinned-range IK involves reverse dependencies that this first
  evaluator does not combine with live constraints. Affected constraints
  pause, and the dialog reports the conflict. Release legacy pins before
  using live constraints on that hierarchy. Existing IK on unconstrained
  scenes retains its existing behavior.
- Rig extensions are written only for bones or objects participating in a
  new rig. **Scenes using this extension require this experimental build**;
  older OpenToonz loaders do not understand the new `<rig>` scene tag.
  Untouched legacy scenes do not acquire rig tags. Identity metadata can
  remain after constraints are removed.

## Verification

The dependency and constraint kernel can be built without Qt or a full
OpenToonz build:

```sh
cmake -S toonz/sources/toonzlib/tests/rig -B rig-build -DCMAKE_BUILD_TYPE=Release
cmake --build rig-build --config Release --parallel 2
ctest --test-dir rig-build --build-config Release --output-on-failure
```

For native stage-object, serialization, cache, and identity regression tests,
configure a normal OpenToonz build with `-DBUILD_RIG_TESTS=ON`, build
`skeleton_stage_test` and `skeleton_rig_test`, and run:

```sh
ctest --test-dir toonz/build -C RelWithDebInfo --output-on-failure -R '^skeleton_(rig|stage)$'
```

Windows and Linux application CI enable these tests. A separate workflow
runs the kernel on Windows, Linux, and macOS, and with address/undefined
behavior sanitizers on Linux.

Manual review should cover:

- Raster, Toonz Raster, and vector cutout rigs; hooks; swapped drawings;
  column locks; nested xsheets; rotated/scaled parents; camera zoom and DPI.
- Build/Animate/IK, persistent tip dragging, Shift snapping, selection,
  Ctrl-drag parenting, drawing browser, Global Key, and Undo/Redo.
- A two-link arm aiming at an independent controller; a hand following an
  offset prop; a rotation limit after Aim At; and a distance-limited prop.
- Enforcement handoffs between two targets; disabled constraints; range
  endpoints; random-order scrubbing; save/reopen; and rendered frames.
- Renaming/reordering/deleting/restoring targets, repeated group paste,
  partial copies into another xsheet, and cycle attempts through parenting.
- Old unconstrained scenes and pinned IK scenes with Modern Bones both off
  and on.

## Boundaries and next stages

The generic Aim At/Buffer experiment in OT-Dev #212 is related work. This PR
is based directly on `main` and does not include that PR's Buffer fields or
Stage Schematic menu implementation. These constraint models need to be
reconciled before either is promoted upstream.

Still deferred: independent controller/helper bones without a drawing column,
persistent IK target/chain constraints, joint limits fed back into the IK
solver, bake-to-keys, Function Editor enforcement channels, named vector
control-point group binding, and one-way Skeleton/Plastic vertex links.
Plastic and vector group data are not converted or replaced. Those binding
stages need endpoint dependency tracking and their own deformation tests.

Prepared with AI assistance from OpenAI Codex at Rodney Baker's request.
Animation:Master is behavioral inspiration; no Hash source code or assets
are included. Reference: [Animation:Master Technical Reference](https://www.hash.com/down/uploads/techref_485_1404937130.pdf).
