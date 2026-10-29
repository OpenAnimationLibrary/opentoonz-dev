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

Changing the selection or navigating preserves the stored command. Repeat checks
the current command handler and enabled state before running. Undo, Redo, another
edit that changes undo history, a drawing gesture, or a scene/history reset clears
the stored command. An ordering command at the front/back boundary does nothing,
just as the original command does.

This first version repeats commands, rather than arbitrary drawing gestures or
saved undo snapshots. Dialogs, toggles, clipboard operations, and file commands
do not opt in. New direct editing commands can opt in with
`CommandManager::setRepeatable()` after their actions have been defined.

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
unsupported/tool edits, history eviction, ordering boundaries, and history reset.

Interactive verification in an OpenToonz build:

1. Draw five overlapping vector strokes. Select the top stroke and use Move Back
   One followed by Repeat twice. Verify each step moves it one position backward.
2. Undo each move individually, then redo each move. Repeat should be cleared.
3. Run Move Back One again, select a different stroke, then Repeat. Confirm the
   new selection moves. Try a locked/non-vector selection and confirm Repeat
   cannot invoke an unavailable ordering command.
4. Assign Repeat a shortcut and perform the same sequence from the keyboard.
5. Make a brush stroke or another unsupported edit. Confirm the old command is
   cleared. Also confirm it clears when loading or creating a scene.
6. Repeat cell rolling, exposure step changes, and drawing substitutions; check
   their individual Undo/Redo steps.
