# Trail Size Multiplier and Frame Offset

The Settings order for both raster-source and vector-source Trails is Distance,
Rotation, Trail Cycle, Frame Offset, then **Size Multiplier**. Converted Toonz
Raster sources use the raster Trail path. Frame Offset is retained from PR #221
commit `c428288a53200c9681c6349ed8d8d7392157934d`.

## Size Multiplier

The multiplier accepts fractional values from **0.25 to 10.0**, default **1.0**.
Values 0.25 and 0.5 reduce width and height to one quarter and one half. Values
2.0 and 10.0 increase each dimension twofold and tenfold. Intermediate values,
including 0.75, 1.5 and 2.25, work in the existing Settings numeric field.

This is a live style appearance parameter: it affects existing and new strokes
using that style. Duplicate the palette style to resize a separate set of strokes.
Source files, global Brush size and stored stroke thickness are not rewritten.
Copy/clone, Auto/Apply, undo and double-parameter palette interpolation use the
existing style-editing mechanisms.

Click stamping still produces one stamp with Frame Range off: its one-stage-unit
carrier is unchanged, and minimum placement spacing stays two units. Artwork
scales about the click anchor using the existing Rotation. Along-stroke spacing
uses the scaled artwork width; Distance remains the extra gap or overlap.
Conservative rotated-source bounds and stamp-aware early clipping include enlarged
artwork even when its carrier lies outside the clipping rectangle.

The multiplier is saved as optional `trailSizeMultiplier` palette/keyframe
attributes and independently tagged `trail-size-multiplier-v1` PLI style metadata.
Locale-independent decimal strings avoid loss of fractional values in the old
PLI numeric style encoding. Missing metadata defaults to 1.0. Nonfinite setter
input resets to 1.0; finite values outside the range clamp to 0.25..10. Existing
style IDs and unversioned payloads are unchanged. Older applications will not
apply the multiplier and may discard the metadata on resave; a released older
application round trip remains unverified.

Raster sources still use the legacy 8-bit, maximum-256-per-axis preparation.
The multiplier changes geometry, not source resolution. Rebuild dependent native
modules against matching headers and libraries.

## Frame Offset

Frame Offset stays directly below Trail Cycle and is independent of scale.
Zero means Automatic. Positive values select exact numeric source drawing IDs:
with drawings 3, 7 and 42, entering 7 selects drawing 7; entering 2 is ignored.
Missing values are not clamped, wrapped or replaced by a nearby drawing, and stay
saved for later source reloads. Blank source drawings count as valid frames.
Letter-suffixed IDs remain part of the cycle but cannot be selected explicitly
with this integer control.

Forward/Backward start at a valid chosen drawing and advance normally. Repeat
holds the chosen drawing. Off sets each new stroke's start without advancing the
shared cursor. Frame Range ignores this drawing default. Changing Frame Offset
never rewrites the recorded sequence of completed strokes.

## Tests

The default `toonz/tests/trail_settings` configuration builds the cycle-state,
click-carrier and multiplier-math tests. With a matching `TNZCORE_LIBRARY`, it also
builds `pli`, `trailstyle`, `trailoffset` and `trailscale` tests. The real
PNG/TIFF/TLV source suite remains separately opt-in as documented in the test
README.

The scale suite checks actual raster/vector parameter types and order, copy/clone,
transforms, spacing, rotated bounds, palette/PLI persistence, locale-independent
fractions, palette animation and preservation of Frame Offset. Standalone tests
are not a substitute for a full application build or GUI/OpenGL acceptance.

Manual checks: stamp at 0.25, 0.5, 1, 2.25 and 10; test rotated wide artwork near
viewer/output-tile boundaries; change an existing Trail via Auto/Apply; undo/redo;
save/reopen; and test valid, missing and Repeat frame choices before and after
size edits. Compare Viewer, Preview and final render.
