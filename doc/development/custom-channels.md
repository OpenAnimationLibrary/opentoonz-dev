# Exploratory custom channels

Stacked on OT-Dev #143, Extended Channel Mapping. This step adds numeric,
unitless custom curves to stage objects without extending the legacy enum.
It is an experimental scene-format extension, not an older-version export path.

## Usage

1. Open the Function Editor, expand Stage, and right-click a column or pegbar.
2. Choose **Add Custom Channel...**, enter `height` and an initial default value.
3. Expand **Custom Channels**, activate the curve, and edit values/keyframes in
   the Function Editor graph or numerical sheet using its existing controls.
4. Reference it as `col1.height` (replace the owner as needed). `col1.channel50`
   also works when its ID is 50. `col1.height(11)` samples UI frame 11. Numeric
   references can also address the mapped historical channels, e.g. channel11
   is X; existing `col1.x` syntax is unchanged.
5. Right-click the custom curve and choose **Rename Custom Channel...**.
   The ID and curve remain the same. Earlier names keep resolving to that
   channel, including after saving/reopening. They cannot be reused by another
   channel in the same owner. Undo/Redo restores the name/alias snapshot.

Names are case-insensitive for expression resolution, begin with an ASCII
letter, and contain up to 64 ASCII letters, digits or underscores. Display case
is retained. Built-in aliases and the `channel` prefix are reserved. There is no
separate editable display label in this iteration. IDs start at 50 per owner;
0–49 retain the foundation's allocation policy. Allocations do not wrap or reuse
IDs during an owner's editing history. No general deletion or channel merge UI
is exposed. Creation can be undone.

Scenes without custom channels retain their existing serialized structure.
Scenes saved with custom channels require this extension: older readers reject
its `customChannels` tag. Creation explicitly explains this in the dialog.
Use separate scene copies for experimentation. Unknown format versions and
malformed/duplicate IDs or aliases are rejected; opaque future-record
preservation and export/baking for older versions remain future work.

## Code-path coverage

This is an implementation coverage map, **not** a measured test-coverage
percentage. The integration executable below has not been run locally.

| Area | Code | Coverage in this change / next work |
|---|---|---|
| Identity and storage | `tstageobject.h/.cpp` | Owner-local IDs, numeric TDoubleParam storage, lookup, default, aliases, observer registration and detachment. No audio/drawing/appearance assignment. |
| Creation and name editing | `functiontreeviewer.cpp` | Add/Rename context actions, validation, existing owner lock check, dirty state, undo/redo; retained aliases protect saved expression text. Separate display labels and alias retirement remain future work. |
| Curve display and editing | `FunctionTreeModel::refreshStageObjects` | Custom group refreshes existing owners, preserves parameter identity, uses existing graph/numerical editing, curve I/O and Function Editor keyframe undo. UI regression testing is pending. |
| Expression parsing and dependencies | `txsheetexpr.cpp` | Named and numeric references reuse ParamCalculatorNode / ColumnParamCalculatorNode, frame arguments, dependency visitors and change propagation. Missing names/IDs are parse errors. No auto-creation or fallback to another property. |
| Circular-reference checks | `functionsegmentviewer.cpp`, `txsheetexpr.cpp` | Existing Function Editor dependency validation applies to custom references; its evaluator is reused. This is not new hardening of arbitrary corrupt expression graphs loaded from files. |
| Persistence | `TStageObject::saveCustomChannels/loadCustomChannels` | Versioned collection, explicit IDs, next-ID counter, current/previous names and curve data. Validation precedes installation. Old-format scenes are still readable. Future unknown-record preservation is not implemented. |
| Otherwise empty columns | `TStageObjectTree::saveData`, `FunctionTreeModel::refreshStageObjects` | Custom data prevents the stage-object record from being omitted solely for lack of occupied cells. It does not extend scene playback duration. |
| Whole-object copying | `TStageObject::clone/getParams/assignParams`, `TStageObjectParams` | Collection and allocation counter accompany object snapshots/copies. Deep copies get distinct parameters; snapshot restoration retains references. Cross-scene UI copy/paste validation is pending. |
| Grammar lifecycle | `TStageObjectTree::createGrammar`, `assignCustomChannels` | Custom curves receive the destination tree's grammar on registration, restoration and copying. |
| Column reference remapping | `expressionreferencemanager.cpp` | Includes custom parameters in same-Xsheet moves and corresponding-ID maps for transfer between Xsheets. Uses existing reference-management policy; collapse/explode and disabled-management behavior require UI tests. |
| Global/Animate-tool keys | `TStageObject::setKeyframeWithoutUndo/removeKeyframe/updateKeyframes`, `tstageobjectkeyframe.h` | Still uses the historical channel set. Global Key/Set Key and Xsheet key diamonds do not represent all custom keys. Do not use those operations to manage custom curves yet. |
| Xsheet key clipboard and timing | `toonz/keyframedata.cpp`, `cellkeyframedata.cpp`, `keyframeselection.cpp` | Fixed stage-key snapshots remain unchanged. Whole-column copying carries the curves, but Xsheet-wide key copy/paste, shifting and retiming need generic channel policies. Function Editor curve operations are the supported path. |
| Cycles and scene duration | `TStageObject::paramsTime`, `TXsheet::updateFrameCount` | Stage-object cycle boundaries and playback duration are not driven by custom keys. Explicit expression sampling uses the same time convention as existing references. |
| Connections and merge | Future channel registry/connection layer | No automatic type conversion, channel reassignment, ID conflict remapping when merging collections, or missing-channel repair UI. IDs are owner-relative, not global UUIDs. |
| Scripting and consumers | Future work | No new script bindings, drawing substitution, audio gain, opacity/tint, vector/color/string channels, or plug-in schema registration. A custom curve affects animation only through an explicit expression consumer. |

## Validation

Enable `-DWITH_CHANNEL_MAPPING_TEST=ON` in an OpenToonz build. Build
`channel_mapping_test` and `custom_channels_test`, then run
`ctest -R 'channel_mapping|custom_channels' --output-on-failure` with the built
runtime libraries available. These targets are opt-in and are not automatically
run by the standard CI configuration.

The new integration test covers creation, invalid names and non-finite defaults,
case-insensitive aliases, numeric references, explicit-frame sampling, dependency
and column-reference discovery, rename/snapshot restore, independent copied
curves, save/reopen of otherwise empty columns, allocation continuity,
duplicate/reserved IDs and exhaustion. Full application and UI tests remain
required before removing draft status.

Local validation: a C++17 harness compiled the actual registry/mapping method
bodies using parameter/tree stand-ins, and passed allocation, alias, rename,
snapshot, clone, observer registration, invalid-number and exhaustion checks
under AddressSanitizer/UndefinedBehaviorSanitizer. Leak detection was disabled
because the execution environment prevents LeakSanitizer process inspection.
This does not validate Qt, the real expression engine, scene streams, or UI.

Manual checks: create two curves, key them, reference them from X/Y, change their
values, rename, undo/redo both operations, and save/reopen. Reject direct and
indirect circular references through the Function Editor. Repeat with multiple
Function Editors, owner locks, owner deletion/undo, column reordering and
copy/paste to another Xsheet. Check collapse/explode with expression reference
management enabled. Compare a scene with no custom channels against the parent
PR for animation and serialization parity.
