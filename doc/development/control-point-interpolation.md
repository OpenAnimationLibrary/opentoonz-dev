# Animate Tool — Control Point Interpolation

CPI animates named sets of vector control points independently of column
placement. It adds a selectable Animate Tool mode and a **CPI Channels** window.
Source drawings remain unchanged. Scene display, onion skins, render geometry,
bounding boxes and render aliases evaluate the same channels.

## Use

1. Expose a vector drawing in an unlocked column. Choose **Animate Tool >
   Control Point Interpolation**, then **CPI Channels**.
2. Click control points, Shift-click to add/remove points, or drag empty space
   to box-select. **Select All** selects the active group's points, or all points
   when **Unassigned points** is selected.
3. Choose **New Group** and enter a unique name.
4. Choose **Set Key** or **Create Key Extremes**. The current pose becomes
   Extreme A and its duplicate becomes Extreme B, 72 frames later by default.
   Change **Extreme distance** before creating a pair; the last distance is
   remembered in `preferences.ini` as `CpiExtremeDistance`.
5. Click Extreme B in the channel table to visit it. Drag an individual point
   or choose **Entire group**. **Position XYZ** edits group translation;
   **Rotate Group** rotates about the group's local X, Y or Z axis.
6. Visit an interior frame and drag. This creates an offset key relative to
   the interpolated extremes. Changing an extreme subsequently preserves that
   offset rather than freezing the old absolute pose.

The initial operation fills empty exposures through the partner frame using
the same drawing. It refuses a span containing another drawing. Both extremes,
the exposure extension and its Undo/Redo are one operation. Frame 1 with the
72-frame default pairs with frame 73. This is three seconds at 24 fps; the
spacing remains 72 frames at other scene rates.

An extreme cannot be deleted independently: **Remove Key / Pair** at an
endpoint removes both extremes and their interior offsets. At an interior
frame it removes only that offset. Group deletion removes that group's channels.
All channel and naming changes support Undo/Redo. Escape, switching frames,
changing columns or leaving CPI discards an unfinished drag.

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
The group pivot is fixed at creation. Existing native vector groups and their
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

The dedicated CPI window owns pair/key operations. Historical Function Editor
curves, Xsheet diamonds, Global Key, key clipboard, cycle and global retiming
commands do not edit CPI channels. Moving exposures does not retime the absolute
scene-row channels. Playback needs the bound drawing exposed at the desired
frames. Plastic texture deformation and bone bindings are follow-up integration
work; this PR does not extend the Skeleton Tool or introduce 3D surfaces.

## Verification

Enable `-DWITH_CPI_TEST=ON` when configuring OpenToonz, build, then run
`ctest -R '^cpi' --output-on-failure` (include `-C RelWithDebInfo` for Windows).
The OT-Dev Windows workflow enables and runs these tests with the packaged
runtime dependencies.

The model/integration test covers paired extremes, delta keys, quaternion
interpolation and plane inversion, stable identity, source-change rejection,
real PLI and scene-column persistence, Viewer evaluation, render bounds/aliases,
fill preservation, independent column copies and a 10,100-point drawing.
The Qt interaction test exercises the actual CPI controller and widgets using
an application-handle harness, including selection, named groups, paired
exposures, live dragging, cancellation, locks and Undo/Redo. It does not replace
native application testing of OpenGL display, playback or final movie output.

Design requested by Rodney Baker; implementation and validation assisted by
Codex. Later bone controls can produce group poses through the same channel
model without changing point identity.
