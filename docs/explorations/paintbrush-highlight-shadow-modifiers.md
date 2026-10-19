# Exploratory design: Shift/Ctrl Paint Brush highlight and shadow

Status: exploratory; no user-facing behavior is implemented in this first commit.

## Goal

When painting with the **Paint Brush** on a Toonz Raster level:
- Normal stroke: selected palette style.
- Shift held at stroke start: a highlight 10% lighter than the selected style.
- Ctrl held at stroke start: a shadow 10% darker than the selected style.
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

## Color semantics

For a first implementation use per-channel interpolation against white
or black, retaining original alpha:

- Highlight: `round(channel + (255 - channel) * 0.10)`
- Shadow: `round(channel * 0.90)`

Apply to RGB, clamp to [0,255], and never repeatedly accumulate adjustments
on the generated variant. This is a 10% channel adjustment, not a perceptual
10% lightness increase; evaluate perceptual alternatives in a separate PR.

Examples (RGB):
- (100, 150, 200) -> highlight (116, 161, 206)
- (100, 150, 200) -> shadow (90, 135, 180)

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
and existing modifier shortcuts. No functional implementation is included
until these edge cases are addressed.
