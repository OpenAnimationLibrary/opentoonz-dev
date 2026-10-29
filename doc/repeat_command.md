# Repeat command

**Edit > Repeat** runs the last eligible editing command again on the current
selection. For example, select a vector stroke, use **Move Back One**, then use
Repeat twice: the stroke moves backward three positions in total. Each change
has its own normal Undo/Redo step.

Repeat is available in **Configure Shortcuts > Edit**. It has no default shortcut.
Its tooltip identifies the stored command. Existing room menus gain Repeat next
to Redo unless they already place Repeat elsewhere.

## Supported commands

- Vector ordering: Move Back One, Move Forward One, Move to Back, Move to Front.
- Group and Ungroup.
- The six alignment and two distribution commands.
- Increase Step, Decrease Step, Roll Up, and Roll Down for selected cells.
- Drawing Substitution Forward/Backward and Similar Drawing Substitution
  Forward/Backward.
- Selection Tool moves, rotations, and scaling for vector, Toonz Raster, and
  raster selections. Repeat reapplies the transform relative to the current
  selection center. Arrow-key nudges retain the original distance, including
  Shift (10 units) and Ctrl (0.1 units).
- Animate Tool transforms on columns, pegs, cameras, and the table: position,
  depth, rotation, global/axis scale, shear, center, and compatible motion-path
  position edits. Dragging/scrubbing/nudging repeats offsets or scale factors;
  typed values repeat an absolute setting. Repeats target the current object and
  scene frame, honor current axis/column locks, and retain the operation's Global
  Key setting. New keyframes and the transform form one normal Undo step.
- Cut, Copy, Paste, Delete and applicable Insert/Paste variants in drawing,
  Level Strip, cell/column, keyframe, Function Editor, palette, hook, and
  Stage/FX Schematic selections. Paste uses the current clipboard. File Browser
  commands do not opt in.
- Cell Reverse, Swing, Random, Autoexpose, Reset/Step/Each, Reframe on 1–4,
  Fill In Empty Cells, Create Blank Drawing, Duplicate Drawing, and Clone Cells.
- Applicable Level Strip timing, duplicate/merge/insert operations.
- Keyframe shifting and setting, scene frame insertion/removal, multiple keys,
  interpolation/easing presets, and the flip/90-degree rotation commands.
- Control-point deletion, linear/nonlinear points, hook editing, and Plastic
  mesh edge/vertex editing commands.
- Palette color/name/value paste, style blending, unused-style deletion, and
  applicable Studio Palette value/reference operations.

## Applied parameters

Brightness/Contrast, Adjust Levels, Antialias, Color Fade, Binarize, Adjust
Thickness, and Time Stretch remember their last successful Apply operation.
Repeat uses those applied values on the current selection without reopening
the dialog. Later changes to the dialog controls do not change the recipe until
Apply is used again. Time Stretch repeats the same length ratio, so successive
repeats compound the stretch.

Raster adjustments have grouped undo entries in both the Xsheet and Level Strip.
Their undo objects retain the actual level/frame identity, rather than resolving
an old cell coordinate against whichever sheet is current later. Read-only levels
and locked target columns are skipped.

The cache retains the **last repeatable operation** until another repeatable
operation replaces it. Frame/selection changes, unsupported commands or drawing
gestures, Undo/Redo, and undo-memory resets (including resets on Save) do not erase
or replace it. An incompatible or locked target temporarily disables Repeat; the cached operation becomes available again
when a compatible target is active. Scene changes and destruction of a
recipe's lifetime owner clear it. An ordering command at the front/back boundary
does nothing, just as the original command does.

Brush/eraser paths, fills, arbitrary free deformations, other dialogs, file
operations, and UI toggles are not replayed. Their coordinate-dependent or modal
behavior needs a dedicated recipe rather than a saved undo snapshot.

New direct commands can opt in with `CommandManager::setRepeatable()`. For shared
command IDs, use `TSelection::enableRepeatableCommand()` so permission follows
the current editor's handler. Applied operations can call
`CommandManager::recordRepeatAction()` after completing their undo block, with
copied parameter values, a QObject lifetime owner, and a current-target predicate.
Recipes never retain an undo item or an old selection pointer.

Animate Tool support completes the agreed initial Repeat scope. Further single
operations can be added to Repeat. Future macro recording/playback is separate
work for ordered sequences and more detailed reproduction of an editing process.

## Validation

The standalone Linux test links the production command dispatcher and undo
manager with Qt 5 Widgets, without building the rest of OpenToonz:

```sh
cmake -S toonz/sources/toonzqt/tests -B build/repeatcommand
cmake --build build/repeatcommand
ctest --test-dir build/repeatcommand --output-on-failure
```

It covers successive relative edits, individual Undo/Redo steps, selection and
handler changes, unavailable targets, nested commands, recursive Repeat calls,
unsupported/tool edits, history eviction, ordering boundaries, undo-memory reset, and scene changes.
Additional tests cover real selection-handler switching (including an ineligible
File Browser stand-in), captured parameters, changing targets, recipe ownership
and replacement, and production affine capture/rebasing for nudges, compounded
rotation/scaling, and singular selections.
Animate tests verify accumulated offsets, compounded/signed scale, absolute typed
settings, zero-scale fallback, and non-finite result rejection. Cache tests retain
the last supported operation across unsupported history edits and Undo/Redo before
replaying it on another target.

Interactive verification in an OpenToonz build:

1. Draw five overlapping vector strokes. Select the top stroke and use Move Back
   One followed by Repeat twice. Verify each step moves it one position backward.
2. Undo each move individually, then redo each move. Repeat should retain the
   ordering operation and remain available whenever its current target is valid.
3. Run Move Back One again, select a different stroke, then Repeat. Confirm the
   new selection moves. Try a locked/non-vector selection and confirm Repeat
   cannot invoke an unavailable ordering command.
4. Assign Repeat a shortcut and perform the same sequence from the keyboard.
5. Make a brush stroke or another unsupported edit, then return to a compatible
   selection and Repeat. Confirm the cached operation survives. Confirm it clears
   when loading or creating a scene.
6. Repeat cell rolling, exposure step changes, and drawing substitutions; check
   their individual Undo/Redo steps.
7. Move/rotate/scale vector and raster selections, then Repeat on the same and a
   different selection. Check nudges with Shift and Ctrl and Undo each repeat.
8. Apply a raster adjustment, close its dialog, then Repeat on another drawing.
   Test both cell and Level Strip selections, duplicate exposures, and Undo/Redo.
   Reopen the dialog and edit its controls without applying; Repeat should retain
   the last applied values. Check read-only levels and locked columns.
9. Repeat a paste in each supported editor, then focus the File Browser and verify
   it cannot inherit the clipboard/delete Repeat permission.
10. Repeat Adjust Thickness and Time Stretch; verify the copied parameters and
    compounded stretch ratio, rather than the old selection or open dialog.
11. With Animate, move a column and Repeat on the same frame, another frame, a
    peg, and a camera. Check relative rotation, compounded global/axis scaling,
    shear, and center moves. Test both Global Key settings and Undo/Redo, including
    removal/restoration of any newly created keys. Locked axes/columns must keep
    their values; zero camera scale must be prevented.
    Also repeat an absolute setting on an unkeyed frame whose interpolated value
    already matches it: the new key must still be created and undoable.
12. Scrub an Animate field and Repeat its difference; type a value and Repeat that
    exact value on another object/frame. Check paired scale constraints and a
    motion-path position on another compatible path object. Draw an unsupported
    stroke between edits and verify the Animate recipe is still available when
    returning to Animate.
