# Layers: hover preview and exposure commands

This extends PR #155 without changing native group names or PLI metadata.

## Behavior

- Leave the pointer over a level thumbnail for 600 ms to display a 320 x 240
  logical-pixel preview. The popup collects every Xsheet exposure of that
  specific level in that column, in Xsheet order. Move horizontally across the
  preview, drag the slider, or use the mouse wheel over the preview to scrub
  those exposures. Held exposures remain visible as repeated Xsheet frames, so
  the preview reflects scene timing rather than only unique drawing IDs.
- High-DPI screens request correspondingly sized native responsive icons for
  each scrubbed drawing. The caption identifies the level, drawing, Xsheet
  frame, column and exposure position.
- Hover/scrub never selects a layer, changes the current scene frame, enters a
  vector group, or modifies the drawing. A short leave grace period lets the
  pointer travel from the small thumbnail into the larger preview. The popup
  closes after leaving both targets, or immediately on normal panel
  interaction, scrolling, keyboard navigation, hiding the panel, or a relevant
  model/frame change.
- Click a column/level/drawing to select the corresponding native exposure cell
  and stage object. Levels use their closest exposure; drawings and vector
  descendants require an exact drawing ID match. Visible Xsheet and Timeline
  panels scroll to the selected exposure.
- Group/stroke selection still uses the native Vector Selection tool and editing
  depth; browsing the tree does not enter groups. Its hosting exposure is also
  reflected in visible Xsheet/Timeline panels.
- Right-click a row (or use the keyboard context-menu key) to target its exposure
  and show the actual Xsheet/Timeline cell menu. Native command availability,
  level-type checks and command implementations are reused. Group rows retain
  Rename Group as their first item. This is an exposure menu, not a promise to
  apply cell commands to an individual vector stroke.
- A Layers-only layout lazily owns a hidden native Xsheet viewer to supply its
  selection and commands. The fallback is not a new visible panel.

## Regression checks

The existing `layers-integration` harness adds checks for exposure targeting,
non-dirty navigation, hover timer cancellation and context-menu forwarding.
Rendering remains outside that non-GPU harness. Existing native group/PLI tests
remain unchanged in scope.

Manual application acceptance is still required:

1. Hover Vector, Toonz Raster and full-color raster thumbnails on standard/high
   DPI displays. Scrub left/right across the image, drag the slider and use the
   wheel; confirm the preview follows the hovered level's Xsheet exposures,
   including holds, without moving the actual current frame or selection.
2. Put two different levels in the same column and confirm each hover preview
   only scrubs exposures belonging to the hovered level. Leave/click/scroll
   before and after the delay; switch scenes, frames and rooms while the pointer
   is stationary; confirm that no old preview remains.
3. In both Xsheet orientations, select a different layer, a repeated exposure,
   a drawing and a nested group. Check the active object and selected cell;
   check the existing group editing-depth guards.
4. Compare exposure menus with native right-click for raster, vector, sound,
   Sub-Xsheet, locked and empty columns. Exercise Copy, Paste, Duplicate,
   Replace Level, Cell Mark and undo against the intended cell only.
5. Keep both Xsheet and Timeline visible; then try a Layers-only room and close
   the panel. Confirm synchronized cells and safe fallback cleanup.

No Windows runtime, GPU-preview or manual acceptance pass is implied by these
source changes or by the non-GPU integration checks.
