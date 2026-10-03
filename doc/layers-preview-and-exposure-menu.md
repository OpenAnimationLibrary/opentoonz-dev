# Layers: hover preview and exposure commands

This extends PR #155 without changing native group names or PLI metadata.

## Behavior

- Leave the pointer over a level thumbnail for 600 ms to display a 320 x 240
  logical-pixel preview. High-DPI screens request correspondingly sized native
  responsive icons. The caption identifies the level, drawing and exposure.
- Hovering never selects a layer, changes the current frame, enters a vector
  group, or modifies the drawing. The popup is non-focusing, stays on screen,
  and disappears on leaving the thumbnail, clicking, scrolling, keyboard
  navigation, hiding the panel, or a relevant model/frame change.
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
   DPI displays; confirm larger artwork, correct captions and no selection.
2. Leave/click/scroll before and after the delay; switch scenes, frames and rooms
   while the pointer is stationary; confirm that no old preview remains.
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
