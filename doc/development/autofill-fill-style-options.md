# AutoFill: independent fill style

Status: proposed follow-up to [OT-Dev #214](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/214) and [OpenToonz #7192](https://github.com/opentoonz/opentoonz/pull/7192). This PR records the design; it does not add working controls or change drawing behavior.

## Goal

Let an artist draw an outline with one palette style and automatically paint the enclosed region with another. For example, draw with black style 1 and fill with skin-color style 2 in one operation.

Keep OpenToonz's current stroke generation, natural closure, tangent-guided forced closure, isolated vector group filling, draw order and undo behavior. Changing the fill style must not change the outline style or geometry.

## Proposed options

Add a Fill Style selector associated with Auto Fill:

| Mode | Fill style |
| --- | --- |
| Current Style (default) | The style captured for the drawn stroke, preserving existing vector AutoFill behavior. |
| Next Style (N+1) | The next numeric style ID after the stroke's style ID; this is not the next swatch in palette-page order. |
| Specific Palette Style | A separately selected style from the current level's palette, displayed with its ID and name. |

Example: with ink style 1, Current Style fills with 1; Next Style fills with 2; Specific Palette Style can fill with 7 while the outline remains 1.

Auto Fill remains opt-in. The selector is disabled when Auto Fill is off. The existing AutoClose/AutoGroup dependencies are outside this enhancement.

## Integration

Start with vector Brush AutoFill. Resolve a fill style separately from the stroke's ink style and pass it to the existing vector region fill. Continue to fill only the newly drawn group's enclosed regions.

Use a small, palette-aware style resolver that can later serve Toonz Raster AutoFill. Do not import raster flood-fill or gap-closing algorithms into vector processing.

Capture the mode and chosen style for the drawing operation. Resolve against the target level's palette, including frame-range and guided drawing operations. Undo/redo must restore the originally applied styles even if the tool selection subsequently changes.

Keep these tool options outside existing brush presets initially, consistent with the current AutoFill switches. Persist the mode; any remembered explicit style must be validated against the active palette rather than treated as a globally meaningful ID.

## Palette and UI safeguards

- Check numeric IDs against the palette's style count before calling getStyle(); never rely on an out-of-range lookup returning null.
- If N+1 does not exist, fall back to the stroke's current style.
- If an explicit selection is deleted or becomes invalid, visibly reset to Current Style.
- On palette replacement or level switching, rebuild the selector safely. Do not silently apply a remembered ID to an unrelated palette.
- Display style names and IDs, and refresh after additions, renaming, deletion or reordering. Do not retain unowned style pointers.
- Rebuilding the selector must not recursively trigger palette/tool updates or rebuild widgets during an active signal callback.
- Decide and document transparent style 0 and non-paint-style eligibility before implementing the picker. Never create or alter palette styles automatically.

## Implementation stages

1. Review and agree on this design, including explicit-style persistence and style eligibility.
2. Implement the vector Brush selector, validated style resolution and operation-state capture.
3. Verify closure plus isolated fill, draw order, split strokes, nested groups, undo/redo, frame ranges and guided drawing.
4. Reuse the selection rules for Toonz Raster in a separate change, preserving its ink/paint distinction and anti-aliasing.

## Acceptance checks

- Default Current Style reproduces the existing result.
- Each mode changes paint without changing line style, geometry or palette contents.
- Missing N+1 and deleted explicit styles cannot cause an out-of-range access.
- Switching levels/palettes, renaming styles and opening older scenes leaves the tool usable.
- Undo/redo restores the same result after changing the selected palette style.
- Previously painted unrelated shapes remain untouched.
- Turning Auto Fill off preserves ordinary drawing behavior.

## Reference and attribution

The [Ztoryc Toonz Raster Brush](https://github.com/matitanimata/ztoryc/blob/master/toonz/sources/tnztools/toonzrasterbrushtool.cpp) exposes Current Style, Next Style (N+1), and explicit palette-style selection. Its history also contains fixes for selector reentrancy, palette ownership and AutoFill undo. Review those fixes before adapting implementation.

This proposal borrows the selection concept, not Ztoryc's raster filling algorithm. Preserve verified contributor credits if code is subsequently ported. Existing vector AutoFill credits to Jeremy Bullock (Motionmonster) and Shun-Iwasawa remain in the underlying feature.

AI-assisted investigation and planning: Codex, guided by Rodney Baker's request to keep separate fill-style selection in a follow-up PR.
