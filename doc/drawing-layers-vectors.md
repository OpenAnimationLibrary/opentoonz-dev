# Drawing Layers: vectors

Drawing Layers is an alternate view of the Xsheet. The vector work incorporates
the read-only hierarchy and native-selection approach explored in
[PR #71](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/71).
This is an exploratory step toward **nameable vector groups**. Editable names
and persistence are not implemented in this iteration; the generated group
labels describe their contents.

## What the panel represents

| Panel row | OpenToonz object | Closest familiar parallel |
| --- | --- | --- |
| Column | Xsheet column, with its native visibility and lock controls | A top-level layer container |
| Level | A distinct level exposed in that column | An animated asset, rather than a single static layer |
| Drawing | The displayed drawing of a vector level | One drawing's artwork hierarchy |
| Group | An actual vector group, including nested groups | An Illustrator group |
| Stroke | One native vector stroke | An open or closed vector path |

A level can have different groups in every drawing. The panel follows the
current exposure in each column. For a level not exposed at the current frame,
it shows its first exposure as a representative drawing. While editing that
level in the Filmstrip, it follows the current drawing there instead. It does
not list every drawing of an animation at once.

Columns and vector children appear in reverse native order, with higher stroke
indices first. This is the native stroke stacking order; computed fill regions
are not independent layer rows. Ungrouped strokes appear directly under the
drawing. OpenToonz's internal implicit/ghost groups are not displayed as folders.

## Browsing and selection

- Expanding a vector level creates its drawing row. Expanding the drawing or a
  group creates only that branch's immediate children. Collapsed branches do
  not allocate their descendant rows.
- Expanding or collapsing a branch does not switch frames, change the current
  selection, enter a group, dirty the scene, or create metadata files.
- A group/stroke row selects through the current tool-owned `StrokeSelection`.
  The drawing must already be active in the Xsheet or Filmstrip, and the Vector
  Selection tool must own the current selection.
- Selection follows native editing depth. At the drawing root, a group selects
  all its member strokes. Enter that group in the Viewer to select its immediate
  children; enter further groups to select deeper strokes. Tree expansion and
  Enter/Exit Group are separate operations.
- Stroke rows show their native index and style index. Tooltips distinguish
  open/closed paths and report control-point counts.
- Frame/model changes refresh the hierarchy. Before a row can change native
  selection, its image and complete stroke/group structure are checked again.
  Transient row indices cannot be used against a changed drawing.

Expansion is retained across refreshes when the same drawing image and structure
remain. A changed structure retains the expanded drawing but collapses its
groups, avoiding accidental reuse of an old group's row position.

## Boundaries of the Photoshop/Illustrator parallel

OpenToonz groups are contiguous ranges in a drawing's stroke order, with nested
group membership. Native grouping can reorder strokes to keep those ranges
contiguous, and can affect how fill regions are computed. They are
not arbitrary folders, and entering a group establishes an editing context.
Renaming must be a metadata operation and must not regroup strokes, change
stacking, change fill topology, or alter that editing context.

Column visibility, render visibility, and locking remain column properties.
Independent group visibility, opacity, masks, and stroke visibility are not
implied by the tree. Drag/drop reparenting, reverse selection highlighting, and
raster channel inspection are outside this iteration.

## Naming: integration requirements

PR #71's explicit `.pli.namedgroups.json` loading/saving is a useful prototype,
but its `(first stroke, last stroke, depth)` locators are deliberately temporary.
The native integer group IDs are image-local; they are not a durable identity
serialized by PLI. Stroke IDs also do not provide a persistent file identity.
Attaching names to these values would risk giving an unrelated group an old name
after editing or reloading.

The next implementation should first establish image-owned group metadata and
its lifecycle, then expose Rename in this panel:

1. Each actual group needs an identity/name that follows its logical group
   through drawing edits, clone/copy operations, and stroke reordering. Define
   duplication as a new identity with a copied display name. Drawing and level
   scope must prevent collisions.
2. Rename must be undoable. Group/ungroup undo must restore the original names;
   ungrouping removes only that group's name and preserves surviving nested
   names. Moving strokes between groups must not transfer the source name to
   an unrelated range.
3. Save and reload must restore names for the correct drawing and hierarchy.
   A sidecar remains a candidate to preserve older PLI readers, but it must be
   coordinated with level saving, Save As, rename/copy, export, and recovery.
   Missing, malformed, incompatible, or stale metadata must never be silently
   applied to another group or overwritten by merely opening the panel.
4. Keep display names separate from identity. Duplicate and Unicode names should
   be valid; unnamed groups should retain generated labels. Two Drawing Layers
   panels must observe the same model-owned name.

Putting this metadata directly in PLI is an alternative requiring a deliberate
compatibility decision. Existing nested group records cannot simply accept an
arbitrary text tag: older readers expect stroke/group records there. This PR
does not change the PLI format or adopt a sidecar schema prematurely.

Naming should be considered integrated only after edit/undo/redo, nested
grouping, save/reopen, duplicated drawings, copied levels, Save As, and missing
metadata have been exercised. PR #71 remains open while these parts are still
unresolved.

## Manual checks

Use a drawing containing ungrouped strokes, sibling groups, and a group within a
group. Expand each depth and compare the rows with native Enter Group behavior.
Verify top-level group selection and individual stroke selection after entering
its parent groups. Switch to another drawing with a different structure, then
group, ungroup, undo, delete, and reorder strokes with the panel open. Repeat in
Filmstrip mode, in a Sub-Xsheet, with two panels, and after hiding/showing a panel.
Confirm inspection never marks the scene dirty and stale rows cannot select
different strokes after a model change.
