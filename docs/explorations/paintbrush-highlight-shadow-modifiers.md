# Exploratory design: Shift/Ctrl Paint Brush highlight and shadow

Status: experimental implementation committed; Palette Gizmo integration and regressions need validation.

## Goal

When painting with the **Paint Brush** on a Toonz Raster level:
- Normal stroke: selected palette style.
- Shift held at stroke start: a new or existing highlight variant of the selected style.
- Ctrl held at stroke start: a new or existing shadow variant of the selected style.
- Modifier release: subsequent strokes use the regular style.
- Shift+Ctrl: explicitly choose normal style until a modifier precedence is agreed.
- Palette's original style, current style selection, and unrelated drawings must not change.

## Code inspection

`toonz/sources/tnztools/paintbrushtool.cpp` constructs a `RasterStrokeGenerator`
at `PaintBrushTool::leftButtonDown`, passing the current *style ID*. The
generator's style ID is subsequently recorded by `BrushUndo` for redo.
Therefore changing only the palette's RGB value **would recolor all other
pixels referencing that style**, and changing the generator to an arbitrary
RGB color is not supported for Toonz Raster indexed pixels.

## Proposed representation

Create or reuse **persistent derived palette styles** for highlights and
shadows. A derived style must have its own palette index and keep a stable
reference to the original style. Do not temporarily overwrite the original
palette style or delete a derived style when the stroke finishes: saved
CM32 pixels refer to palette IDs. Derivation and painting must be part of a
single coherent undo transaction, including handling of redo.

A follow-up implementation should establish how to locate/reuse a derived
style (including source-style edits, palette changes, style naming, and
index limits) using existing palette-management APIs, rather than inventing
new IDs directly in the raster stroke generator.

## Palette Gizmo color semantics

The Palette Gizmo dialog publishes its live **Fade (%)** field to the
Paint Brush through a Qt application property. If the Gizmo has not supplied
a Fade in the current session (for example, it has never opened), the Paint
Brush uses **30%**. Opening the unmodified Gizmo publishes its existing
10% Fade default; the Gizmo's original action button defaults are not
changed. Editing the Fade number or pressing Fade updates the brush strength.

For a **new** companion only:
- Highlight: interpolate the source RGB towards white by Fade (%).
- Shadow: interpolate the source RGB towards the Gizmo's fade-to-color swatch,
  initially black, by Fade (%). Each shadow target component is capped at
  the original source component to avoid inadvertently brightening the
  shadow.
- If the user explicitly edits a Gizmo Value / Saturation / Hue / Alpha
  Scale or Shift field (or activates that field's + or - button), its
  magnitude contributes to new variants. Highlight uses positive values
  and shadow uses negative values. Value and Saturation are adjusted in
  HSV; Hue shifts in degrees; Alpha is modified only when an Alpha field
  was explicitly used. The Gizmo's 10-unit default for these other controls
  does **not** affect paint until the user changes/uses that control.
- Clamp numeric channel values to valid bounds and preserve original alpha
  unless Alpha has been explicitly adjusted.
- The swatch tints shadow variants only; it does not change existing
  palette styles or force colored highlights.
- Neither editing Gizmo settings nor reusing a companion recalculates an
  existing highlight/shadow color. Artists can edit companions manually.

With no Gizmo and no extra adjustments, RGB (100,150,200) and the 30%
fallback produce an illustrative highlight (147,182,217) and shadow
(70,105,140), subject to rounding.

The bridge uses in-process Qt application properties, not a new file
format or a direct dependency from tnztools on OpenToonz executable
symbols. It intentionally resets with the application session.

### Style names

New styles are named `Highlight <sourceStyleId>` and
`Shadow <sourceStyleId>`, e.g. `Highlight 7` and `Shadow 7`.
Previously generated names `[Paint Brush Highlight 7]` and
`[Paint Brush Shadow 7]` are recognized and reused unchanged to avoid
duplicates and preserve the user's existing edits.

## Scope restrictions

- Only the Paint Brush on Toonz Raster initially. Do not affect Brush,
  Fill, or raster/vector tools.
- Capture modifier mode at stroke start so a stroke does not silently
  split across style IDs or produce inconsistent undo/redo results.
- Preserve existing Shift/Ctrl shortcuts and existing Pick, Selective,
  Lock Alpha, Empty Only, Lines/Areas modes. Conflict resolution takes
  precedence over adding the new modifier shortcut.
- Do not change the selection or palette automatically while hovering.
- Avoid a separate derived style for every brush dab/stroke.

## Required tests before this becomes a functional PR

1. Paint normal, highlight, shadow into a TLV; inspect resulting CM32
   style IDs and rendered colors.
2. Original style remains identical; pixels using it retain their colors.
3. Undo/redo paints and derived-style creation safely, including after
   editing other palette entries.
4. Save, close, reopen, and render all three variants.
5. Paint across multiple frames with one palette and reuse IDs.
6. Change source style and check variant synchronization policy.
7. Test Pick, Selective, Empty Only, Lock Alpha, Lines/Areas and Shift/Ctrl
   interactions.
8. Confirm keyboard modifier state and Windows / macOS / Linux behavior.
9. Confirm no unintended impact on Toonz Raster anti-aliasing and
   palette-style ID capacity.
10. Run Windows build, formatting checks, and deterministic brush/undo tests.

## Open decision

An alternative is a **non-destructive per-pixel lighting overlay**. That
would require a different storage model than current CM32 style-index
painting, so it is deliberately out of scope for this initial experiment.

## Non-regression contract and palette triplets

The feature is strictly opt-in per stroke through Shift or Ctrl. Existing
normal Paint Brush behavior, Pick, Selective, Lock Alpha, Empty Only, Lines,
Areas, filling mode, existing shortcuts, undo/redo, and palette management
must remain unchanged. If a modifier is already reserved by a tool feature,
resolve that conflict first; never silently override it.

A source palette style may have two companion variants, displayed in its
palette page as `[Highlight] [Source] [Shadow]`. This ordering is within the
page and **not** a demand for consecutive numerical style IDs. Preserve
the source's ID and the IDs of all other existing styles and indexed pixels.

On the first Shift or Ctrl stroke, obtain the source style's stable
numerical ID and its owning palette, then find or create only the needed
companion (Shift creates highlight; Ctrl creates shadow). Reuse existing
companions for further strokes and across frames using that palette.
`TPalette::Page::insertStyle` supplies page placement without requiring
numeric-ID remapping.

The source-to-companion relationship must be persisted with the palette,
not kept only in PaintBrushTool state or inferred merely from a matching RGB
color or style name. A proposed association record is:
`{sourceStyleId, highlightStyleId?, shadowStyleId?}`, scoped to the palette.
Determine a format compatible with existing palette serialization before
adding implementation code. Never reuse a companion with mismatched source
identity just because its rendered color matches.

Requirements for first implementation:
- Original palette style is never mutated by drawing a variant stroke.
- Ordinary Paint Brush strokes incur no palette creation or new processing.
- Companion creation must be undoable without invalidating pixels already
  referencing a companion. Undo must not remove an in-use shared style.
- Preserve source style properties and alpha; explicitly handle animated,
  texture, procedural, linked, cleanup, and otherwise unsupported styles
  rather than collapsing them unpredictably to flat color.
- Respect locked palettes, missing palette/page, maximum palette capacity,
  read-only levels, invalid indices, and style 0.
- Never silently select a generated companion as the UI's current style.
- Source edits, deletion, and palette cloning/copying must not leave dangling
  variant references. Define a safe lifecycle policy before release.
- Hold the modifier mode for a complete stroke; do not switch style IDs
  mid-stroke and invalidate existing undo assumptions.
- On any unsupported or failed case, leave the palette and raster unchanged
  for the attempted modifier stroke, with normal drawing unaffected.

Add focused regression tests proving that an existing TLV rendered, edited,
saved, and reopened with **no modifier** behaves identically before and
after the feature. Test creation order (Shift first, Ctrl first), reuse,
palette-page ordering, Undo/Redo, save/reopen, source edits, style deletion,
and existing modifier shortcuts. This experimental implementation should not be treated as merge-ready until these
edge cases are addressed.

## Manual color adjustment

Generated companions are ordinary editable palette styles. Artists may manually lighten, darken, or otherwise adjust each generated style afterward. Reusing the companion must **not** recalculate its color or overwrite those manual edits. Creation uses the current Gizmo Fade strength (or a 30% fallback) only once. Changes to the original source color should not silently overwrite manually edited variants; any future regeneration or synchronization must be explicit.

## Current prototype status (October 19, 2026)

`paintbrushtool.cpp` now intercepts Shift/Ctrl at mouse-down, creates a Gizmo-adjusted companion style if missing, and reuses named companions in the same palette. It triggers palette UI refresh when the palette grows. No modifier takes the original unmodified stroke path. Generated colors remain editable and are not regenerated on reuse.

**Not complete or validated:** Style association currently uses encoded style names, not a distinct persisted association record. User renaming a companion can defeat reuse. Dedicated palette undo/redo for style creation and style lifecycle handling is not implemented; automated tests, Windows compilation, clang-format, and manual regression tests are outstanding. The PR should not be merged until these concerns are resolved. Existing builds succeeded before the Gizmo changes; new CI checks and manual interaction testing remain necessary.


## Locked interaction contract: Gizmo settings apply on creation only

The Palette Gizmo is a **preset source**, not a live link or automatic
palette-color updater. Its field changes publish numbers for the next
creation event and must not alter any highlight/shadow companions.

1. **Lookup first:** when Shift/Ctrl requests a companion, search for an
   existing style associated with the current source style ID before reading
   Gizmo settings. If found, return its palette ID unchanged. Never call
   `setMainColor`, `setColorParamValue`, or style replacement on reuse.
2. **Create only when absent:** the first stroke for a missing variant
   snapshots the current Gizmo values (or the 30% fallback) and generates
   a standalone palette style. After that, changes to Fade, Value,
   Saturation, Hue, Alpha, or the target swatch do not recalculate it.
3. **Ordinary Gizmo behavior remains:** the Gizmo's Apply/Fade/+/− buttons
   continue operating on the user's *selected* styles. If a generated
   companion is deliberately selected, these existing tools can edit it.
   This is an explicit edit, not an automatic synchronization.
4. **Manual edits are authoritative:** an artist can change a companion's
   color without losing those edits on subsequent strokes or Gizmo changes.
   Changing the original source style also does not implicitly regenerate
   its companions.
5. **Locked palette:** reuse existing companions without creating styles.
   If a modifier requests a missing companion while the palette is locked,
   do not paint with the wrong (original) color; cancel only that modified
   stroke. Unmodified brush strokes are unaffected.
6. **Independent palette links:** new companions must not retain a studio
   palette link that could unexpectedly synchronize their colors.
7. **No regeneration command in this PR:** a future explicit regenerate
   action, if ever added, must require direct user action and independent
   review. Do not connect automatic recoloring to Gizmo signals.

### Acceptance test sequence (interactive Windows test required)

Use a new TLV with plain solid source style **#7**, RGB (100,150,200).
Note exact RGB values, palette indices, and the selected source style ID:

| Step | Action | Required outcome |
|---|---|---|
| 1 | Restart OpenToonz; leave Gizmo unopened; Shift+paint | Create `Highlight 7` once: RGB (147,182,217) at 30% fallback; source untouched |
| 2 | Ctrl+paint without opening Gizmo | Create `Shadow 7` once: RGB (70,105,140); source untouched |
| 3 | Select `Highlight 7` in Style Editor; manually change its color; return to source style #7 | Manually edited highlight persists |
| 4 | Open Gizmo and change Fade to 10%; edit other Gizmo fields; apply Fade to **a different selected source style** | Previously created `Highlight 7` and `Shadow 7` do not change |
| 5 | Shift+paint and Ctrl+paint using source #7 | Both existing companion IDs and their manually edited colors are reused; no new palette styles |
| 6 | Select another source style #8 and Shift+paint / Ctrl+paint | Only these newly created companions use the current Gizmo settings; source #7 companions stay fixed |
| 7 | Use Undo/Redo on strokes and then save/reopen TLV | Pixel colors/IDs, existing companion IDs, and manual edits remain stable; track separate palette-creation undo issue |
| 8 | Lock palette; Shift/Ctrl paint with an existing companion | Existing companions remain usable; palette does not change |
| 9 | Lock palette; Shift/Ctrl paint where companion is missing | No new style and no normal-color fallback stroke |
| 10 | Paint normally without Shift/Ctrl, before and after changing Gizmo values | Original Paint Brush behavior and selected style are unchanged |

Test with Pick, Selective, Empty Only, Lock Alpha, Lines/Areas and the Gizmo's
Fade-to-Color swatch. Revisit on Windows, Linux and macOS when practical.

**Validation status:** code-path inspection confirms lookup precedes color
generation, the existing-style branch does not write colors, and published
Gizmo values do not themselves invoke companion-color updates. Independent
arithmetic checks confirm the expected 30% fallback RGB examples above.
These are *source inspection and deterministic arithmetic checks*, not
an executed OpenToonz GUI test. A successful CI build is not equivalent to
the interactive acceptance test.

**Known follow-ups outside this lockdown:** detection currently uses
short/legacy style names, so manually renaming a companion can prevent
reuse; a separate persistent relationship is not yet implemented. Adding
a style is not yet integrated into a single palette-and-brush undo group.
Do not label those behaviors validated or merge-ready without their own
review and tests.
