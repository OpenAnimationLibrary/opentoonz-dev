# Animate Tool — Control Point Interpolation

CPI animates named sets of vector control points independently of column
placement. It adds a persistent editing mode with a compact tool-options bar and a
**CPI Channels** inspector.
Source drawings remain unchanged. Scene display, onion skins, render geometry,
bounding boxes and render aliases evaluate the same channels.

## Use

1. Expose a vector drawing in an unlocked column. Choose **Animate Tool >
   Control Point Interpolation**, or its **CPI Mode** button.
2. Select CPs with the **Selection** tool: click a point, Shift-click to toggle
   membership, Ctrl-click to remove, or drag a rectangle/lasso. Shift adds to a
   region selection; Ctrl subtracts. **Select All** uses the active group, or
   all drawing points when **All points** is selected.
3. Choose **New Group** in the CPI toolbar and enter a unique name.
4. Choose **Set Key** to create paired Key Extremes. The partner defaults to
   72 frames later. **Channels > Extreme distance** changes this spacing and
   remembers it as `CpiExtremeDistance` in `preferences.ini`.
5. Use the usual tool buttons/shortcuts for **Selection**, **Animate**,
   **Magnet**, and **Iron** (shown as **Smooth** in the CPI toolbar). CPI and its
   selection remain active when switching these tools or navigating with Hand,
   Zoom, and Rotate View. **Exit CPI** returns to ordinary drawing editing.
6. With Selection/Animate, drag CPs or use the ordinary bounding-box handles:
   corners and edges scale, dragging outside a corner rotates, and dragging
   empty space inside the box moves the selection. Shift constrains scaling
   and snaps rotation; Alt scales about the center. The center handle moves the
   transform pivot. **Selected CPs / Entire group** controls the edit scope.
7. Magnet uses the existing radial falloff on the selected CPs. Smooth relaxes
   them toward neighboring points while pinning open-stroke endpoints. Radius
   and Strength are shared brush controls, remembered as `CpiBrushRadius` and
   `CpiBrushStrength`. Without selected CPs, brushes affect the active group.
8. Visit an interior frame and edit. CPI records offsets from the interpolated
   extremes. **Channels** remains available for key navigation/removal and the
   existing **Position XYZ** and **Rotate Group** controls.

Unsupported drawing tools are unavailable while CPI is active; selecting them
retains the CPI session and presents the supported tool choices. They cannot
run their source-geometry editing gestures through CPI. Magnet and Smooth do
not insert, split, merge, delete, or reduce control points. True erasure and
animated visibility are not part of this increment.

The initial operation fills empty exposures through the partner frame using
the same drawing. It refuses a span containing another drawing. Both extremes,
the exposure extension and its Undo/Redo are one operation. Frame 1 with the
72-frame default pairs with frame 73. This is three seconds at 24 fps; the
spacing remains 72 frames at other scene rates.

An extreme cannot be deleted independently: **Remove Key / Pair** at an
endpoint removes both extremes and their interior offsets. At an interior
frame it removes only that offset. Group deletion removes that group's channels.
All channel and naming changes support Undo/Redo, with one history entry per
completed gesture. Escape, switching tools/frames/columns, or leaving CPI
cancels the unfinished gesture. A second Escape clears the CP selection.
Changing the global selection owner, such as clicking the Xsheet, preserves
CPI's workspace selection. Changing the bound drawing clears that selection.

## Channel model and identity

A column owns an immutable snapshot of CPI data. Named groups refer to a
binding containing a source level reference, drawing ID, UUID, control-point
layout and bind coordinates. Each control point has a 64-bit address composed
of a 32-bit stroke index and a 32-bit point index, scoped by the binding UUID.
Names are display labels and can change without changing identity. Full
addresses are compared; hashes are never treated as proof of point identity.

There is no small, fixed control-point or group count. Storage grows with the
bound drawing and actual keyed offsets, subject to memory and the existing
OpenToonz stroke/index limits. A SHA-256 content digest identifies evaluated
CPI state in render-cache aliases. A hash does not encode the point positions.

Each pair owns two complete poses and a sparse map of interior delta keys.
A pose contains group XYZ translation, a normalized double-precision quaternion,
and sparse per-point XYZ offsets. Translation and offsets interpolate linearly;
rotation uses shortest-arc quaternion interpolation. Interior deltas return to
neutral at both extremes. Between disjoint pairs, the preceding end pose holds.
Before the first pair and after the last, the nearest extreme holds.

For rest point `r`, group pivot `c`, translation `t`, rotation `q`, and local
point offset `d`, the evaluated point is:

`p = c + t + q * (r - c + d)`

The current vector consumer projects XY. Z is retained in the scene data for
future development. Dragging points on a tilted plane solves the projected
local XY basis; it does not discard tilt or invert a quaternion as though it
were a hash. Exactly edge-on planes have no unique screen-to-plane inverse and
reject point dragging. Group translation remains available. Column/parent
placement and depth are applied by the existing stage pipeline afterward.

Groups have disjoint memberships in this first implementation. A closed
stroke's duplicated endpoint belongs to the same group and moves together.
The stored group pivot is fixed at creation. The Selection handle pivot is an
editing aid, independent of the stored group pivot. Existing native vector groups and their
names in the Layers panel are separate; CPI does not change PLI grouping.

## Persistence and boundaries

Data is versioned inside the scene's level-column record, including persistent
level references. No sidecar or source-PLI extension is introduced. Copies of
whole columns retain CPI data and subsequent edits are independent. Empty
trailing columns with CPI are retained when saving.

Scenes using CPI require a build supporting this scene extension. Older builds
reject the new column tag. Scenes without CPI omit the tag and keep existing
behavior. Use a separate scene copy for experimental CPI work.

Binding validation checks ordered geometry, point counts, thickness, stroke styles,
native group structure and closed/open status.
PLI's coordinate quantization is accommodated. Source topology or geometry edits
suspend CPI instead of redirecting animation to unrelated points. Remove that
drawing's CPI groups and recreate bindings after changing the source geometry.
Palette color changes and fill styling remain independent. Reassigning a stroke
style or changing its width or native grouping requires rebinding.

The CPI toolbar and inspector own pair/key operations. Historical Function Editor
curves, Xsheet diamonds, Global Key, key clipboard, cycle and global retiming
commands do not edit CPI channels. Moving exposures does not retime the absolute
scene-row channels. Playback needs the bound drawing exposed at the desired
frames. Plastic texture deformation and bone bindings are follow-up integration
work; this PR does not extend the Skeleton Tool or introduce 3D surfaces.

## Shared editing path

`ToolHandle` retains the requested ordinary tool name while routing supported
editing gestures to the CPI interaction tool. Viewer navigation uses the normal
navigation tools. The interaction tool derives from `SelectionTool` and reuses
its handle drawing/hit testing and the `MoveSelection`, `Rotation`, and `Scale`
helpers. Its selection target and commit path are CPI-specific.

Magnet reuses `TStrokePointDeformation`. Smooth and the ordinary Iron tool use
the shared `smoothControlPoint` calculation; CPI omits Iron's topology-changing
cleanup. Brush stamps are spaced by travelled distance rather than event rate.

A gesture holds the immutable channel snapshot and a working pose. The column
publishes a transient immutable preview of that one pose. Viewer evaluation,
render bounds, and cache aliases consume it only for the matching snapshot and
scene frame. Serialization and column copies exclude previews. Committing
creates one new channel snapshot and one Undo record; cancelling clears the
preview. Whole-group rigid transforms use translation/quaternion channels;
scaling and individual-point changes produce local point offsets. Existing Z
values and tilted-plane mapping are preserved.

## Verification

Enable `-DWITH_CPI_TEST=ON` when configuring OpenToonz, build, then run
`ctest -R '^cpi' --output-on-failure` (include `-C RelWithDebInfo` for Windows).
The OT-Dev Windows workflow enables and runs these tests with the packaged
runtime dependencies.

The model/integration test covers paired extremes, delta keys, quaternion
interpolation and plane inversion, stable identity, source-change rejection,
real PLI and scene-column persistence, Viewer evaluation, render bounds/aliases,
fill preservation, independent column copies and a 10,100-point drawing.
The Qt interaction test exercises the actual tool-routing, handle adapters,
controller, and widgets using an application-handle harness. It covers shared
selection, navigation, named groups, paired exposures, live dragging,
scale/rotation handles, Magnet/Smooth, cancellation, unsupported-tool isolation,
locks, and Undo/Redo. Preview tests check rendering, cache identity, frame
isolation, and exclusion from scene serialization and column copying. It does not replace
native application testing of OpenGL display, playback or final movie output.

Design requested by Rodney Baker; implementation and validation assisted by
Codex. Later bone controls can produce group poses through the same channel
model without changing point identity.
