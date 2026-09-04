# Experimental Vector Named Groups

This draft workstream explores a dockable OpenToonz panel for inspecting and manipulating the existing vector-group hierarchy of the current PLI drawing.

## Initial milestone

Establish the panel/window and the hooks that interface with existing OpenToonz vector-group code.

The first implementation phase should provide:

- a dockable **Experimental Named Groups** panel;
- a synthetic per-frame **Root** entry representing all top-level groups and completely ungrouped strokes;
- a tree/list view of the current vector drawing's real groups and strokes;
- current frame / current level / vector-image refresh hooks;
- selection synchronization between the panel and the drawing where practical;
- a secondary schematic area that can initially mirror the same hierarchy without editing it;
- controller/model boundaries that later support drag/drop reparenting, ordering, Undo/Redo, and group naming.

## Root behavior

`Root` is not an actual vector group. It represents the top level of one vector drawing/frame.

- An ungrouped stroke appears directly below Root.
- A top-level vector group appears directly below Root.
- Moving a stroke to Root will eventually remove all of that stroke's group ancestry.
- Moving a group to Root will eventually reparent the group to the top level while preserving its internal hierarchy.

## Named-group metadata

The PLI remains authoritative for geometry, stroke order, and grouping structure.

User-facing group names are experimental metadata and should not change drawing/rendering semantics. The current storage direction is an adjacent JSON sidecar associated with the PLI level, with a stable identity/resolution layer between stored named-group records and OpenToonz's internal group representation.

The first implementation should inspect and document how internal group identity behaves across save/load, regrouping, Undo/Redo, and ordinary editing before the sidecar locator schema is finalized.

## Deliberately deferred

- drag/drop mutation of the vector hierarchy;
- persistent group-name sidecar implementation;
- editable schematic nodes;
- multi-frame / selected-frame operations;
- PLI format changes;
- production UI naming or final workflow decisions.

This file exists to establish the scope of the experimental draft PR while the functional panel implementation is developed incrementally.
