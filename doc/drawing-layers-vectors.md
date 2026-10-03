# Layers: vectors

Layers is an alternate view of the Xsheet. The vector work incorporates
the read-only hierarchy and native-selection approach explored in
[PR #71](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/71).
The window is now named **Layers** to leave room for more of OpenToonz's layer
concepts. This iteration provides compact column folders, level thumbnails, and
editable names for native vector groups. Existing saved panel layouts and
shortcuts keep working through the original internal panel/command IDs.

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

## Naming vector groups

Double-click a group label, press F2 on a group, or choose **Rename Group...**
from its context menu. Names can contain Unicode and need not be unique. Clear
the name to restore the generated stroke-count label. Escape cancels editing.
Locked columns and read-only drawings cannot be renamed. Renaming a nested group
does not require entering it in the Viewer and does not change editing depth,
stroke order, geometry, fills, or selection ownership.

Names belong to native image groups, including every ancestor of a stroke. They
follow clone, split/merge and native stroke/group reordering operations. Copies
start with the same labels and can subsequently be renamed independently.
Group/ungroup undo records names, membership, entered context and original stroke
order, preserving the actual stroke objects rather than replacing the drawing.
Rename is undoable and marks the level frame and scene dirty; both open Layers
panels observe the same model-owned data.

PLI saving writes an optional, versioned UTF-8 group-name tag immediately after
its native group record. The tag points back to that exact serialized group;
geometry and image records never reference it. This avoids a sidecar and makes
ordinary level saving, Save As and copied PLI files carry names automatically.
The PLI version and all existing tag numbers remain unchanged. Legacy files
without names retain their generated labels. Invalid name versions, targets and
UTF-8 are ignored without changing the artwork.

Older readers skip the new unreferenced tag and can still read the artwork.
**Resaving in an older build drops group names.** The extension does not import
PR #71's experimental `.pli.namedgroups.json` sidecars: their temporary stroke
ranges cannot safely identify a group after editing. PR #71 remains a reference
for the hierarchy and selection behavior, rather than a required second panel.

## Automated checks

Configure with `-DWITH_LAYER_TESTS=ON` to build `layers_test`. The Windows workflow
runs it against the same native libraries and Qt build as OpenToonz, after DLL
packaging. The test executable is kept outside the application package.

The checks cover nested/Unicode names, independent clones, entered-group context,
PLI save/reopen and Save As, files without names, unknown and malformed metadata,
double-click/F2/context-menu editing, two panels, clearing/cancelling names,
locked columns, stale editors, and rename/group/ungroup undo and redo. The UI test uses Qt's native Windows platform in CI and the offscreen platform
for local Linux checks. It disables thumbnail painting and stops background
thumbnail workers for the command checks. CTest bounds execution with a
90-second timeout. Interactive visual and Viewer testing remains useful
alongside these checks.

## Manual checks

Use a drawing containing ungrouped strokes, sibling groups, and a group within a
group. Expand each depth and compare the rows with native Enter Group behavior.
Verify top-level group selection and individual stroke selection after entering
its parent groups. Switch to another drawing with a different structure, then
group, ungroup, undo, delete, and reorder strokes with the panel open. Repeat in
Filmstrip mode, in a Sub-Xsheet, with two panels, and after hiding/showing a panel.
Confirm inspection never marks the scene dirty and stale rows cannot select
different strokes after a model change.

Rename sibling and nested groups, undo/redo each operation, save/reopen the level,
and repeat with Save As and a copied drawing. Ungroup several separate groups
and undo; each name and boundary should return. Compare the compact folder rows
with the level thumbnails in a narrow and a wide panel.
