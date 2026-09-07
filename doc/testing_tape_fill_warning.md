# Tape stroke direction and fill-risk confirmation

This variation of [OpenToonz PR #2355](https://github.com/opentoonz/opentoonz/pull/2355)
preserves stroke direction when the Tape Tool extends the beginning of a vector.
It includes Shun Iwasawa's original change, with its authorship preserved.
Joining two separate vectors can still require reversing one of them.

Existing fill colors can still change or disappear after modifying vector
boundaries. This PR adds informed cancellation, not a guarantee of fill
preservation. The warning covers operations that join or extend existing strokes
when the current drawing contains a nonzero region style. The check includes
nested regions and may warn about fills elsewhere in the drawing. It does not
apply to adding a separate connector with Join Vectors off, or to an actual
line-to-line connection that does not join/extend an existing stroke.

## Remembered choice

The checkbox is initially unchecked. Cancel is the default button. Only clicking
Cancel or Continue with the checkbox checked saves a decision. Escape and the
window close button cancel the current gesture without saving a decision.

The internal `tapeToolFillRiskPolicy` key in the user's `preferences.ini` has
these values:

| Value | Behavior when a qualifying operation encounters fills |
| --- | --- |
| Absent or `0` | Ask each time |
| `1` | Continue without asking |
| `2` | Cancel without asking; show a brief tooltip explaining why |

Unknown values ask. Unfilled drawings do not trigger this warning, including
when a remembered Cancel is active. There is no control in the Preferences
window. To reset the choice, close OpenToonz, remove the key (or set it to `0`)
in the active user's `preferences.ini`, and restart.

## Application validation

Run the following with a test vector level. Begin with the saved key absent.
These checks remain necessary on the native application builds; isolated
geometry/dialog harnesses do not exercise the complete region engine and UI.

### Direction and inbetweening

1. Reproduce [issue #2347](https://github.com/opentoonz/opentoonz/issues/2347):
   draw two separate strokes in the same direction, copy them to a later drawing,
   and generate inbetweens.
2. On the first drawing, use Normal / Endpoint to Line with Join Vectors on.
   Extend the beginning of one stroke to the interior of the other.
3. Regenerate inbetweens. Verify that the extended stroke has not flipped.
4. Repeat with Smooth on and off, and with the other endpoint. Also check
   Endpoint to Endpoint joins and a stroke joined to itself.

### Warning and cancellation

1. Add fills, including a filled subregion inside an unfilled outer region.
   Reopen a saved PLI to cover lazily computed region data.
2. Attempt a qualifying Tape operation. Verify the warning appears before the
   geometry changes, with Cancel as the default.
3. Test Cancel, Enter with the default focus, Escape, and the close button.
   Geometry and colors must stay unchanged. No new Undo entry, dirty marker, or
   loss of existing Redo history should occur.
4. Repeat in Rectangular mode with multiple eligible connections. Verify one
   warning and cancellation of the entire gesture.
5. Try an empty rectangle and a rectangle containing only connections across
   different groups. Neither should create a warning or an Undo entry.
6. Continue once with the checkbox unchecked. The next qualifying gesture must
   ask again. Unfilled line art should proceed without a warning.

### Persistence and recovery

1. Choose Continue with Remember my choice checked. Test another gesture, then
   restart and test again. Neither should prompt.
2. Reset the key, choose Cancel with the checkbox checked, and repeat. The
   operation must be blocked with a brief explanatory tooltip. Unfilled drawings
   must still work.
3. Reset the key. Check the checkbox, then dismiss using Escape or close. The
   next qualifying operation must still ask.
4. Verify that no Tape warning control has appeared in the Preferences window.

### Undo, Redo, and fills

1. Accept a normal operation affecting a filled drawing. Undo must restore the
   original geometry and region style assignments. Redo must restore the accepted
   geometry and colors, including any fill changes the user accepted.
2. Repeat for a rectangular gesture with several connections, holes/nested
   regions, multiple colors, and fills extending outside the selection rectangle.
3. Cycle Undo/Redo several times. Confirm that one gesture remains one history
   step, and that neighboring fills and stroke group membership remain correct.
4. Repeat after saving/reopening the drawing. Check both Xsheet and Level Strip
   editing, and confirm that Tape use on motion paths is unaffected.

## Development checks performed

- C++17 syntax compilation of the three affected implementation files against
  Qt 5 headers.
- A focused harness using the production extension and edge-mapping functions
  with real `TStroke` geometry: both endpoints, Smooth on/off, and filled/unfilled
  edge lists. The region engine was not part of this harness.
- A focused harness exercising the production warning, operation preflight,
  rectangular loop, and Undo/Redo fill restoration with Qt widgets, QSettings,
  and drawing test doubles. This covered defaults, explicit choices, Escape,
  close, settings reload, nested fills, empty/canceled batches, group restrictions,
  and stale target rejection.

Authored with assistance from ChatGPT (Codex).
