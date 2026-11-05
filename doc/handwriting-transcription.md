# Transcribe Handwriting to Note Level (Windows, experimental)

Open **Windows > Transcribe Handwriting** (under **Other Windows** in some
rooms). The command also supports normal shortcut assignment and panel docking.
Recognition uses the Windows handwriting engine locally. No Python, cloud
service, account, API key or separately bundled recognition model is required.

1. Create a Note Level through the existing Xsheet command if needed. Select an
   existing, unlocked Note Level cell and click **Use Selected Note Cell**.
2. Write on the wide surface. The dropdown selects an installed Windows
   recognizer; the pause field controls how soon completed strokes are recognized.
3. The typed preview appears underneath the preserved ink. **Auto transfer to
   Note Level** writes the preview after a one-second pause in recognition/text
   changes. **Transfer Text** writes it immediately.
4. Keep **Follow current frame** enabled to use the bound Note column at the
   current frame, even while selecting drawings in other columns. Disable it to
   pin the displayed destination. Empty cells require an existing Note drawing;
   this panel does not create exposures or new levels automatically.

Existing note text is retained as a prefix. The preview can be edited; subsequent
recognition preserves that manual correction until **Use Recognition** is clicked.
Ink/text visibility, ink opacity and surface width can be adjusted independently.
**Undo Ink** and **Clear Ink** affect the panel's handwriting. Clear does not
erase already transferred Note Level text; use the text preview or normal scene
undo for that. Transferred text uses the existing Note Level storage and normal
undo/redo and marks the scene dirty.

The destination label distinguishes the Xsheet row from the Note drawing. Held
cells referencing the same Note drawing share its text. Transcription does not
split holds or remap exposures. Changing a Note cell externally, locking its
column, changing Xsheet/scene, playback, and hiding the panel invalidate pending
work. External text edits or scene undo pause automatic transfer; review the
preview and explicitly transfer before replacing that text.

Install the optional **Handwriting** component for your language in Windows
language options if no recognizer appears. **Language Settings** opens that
Windows page, and **Recheck Windows** refreshes the recognizer list.

## Scope and testing

This draft is independent of [OT-Dev #231](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/231).
Only transferred text is saved in the scene. Ink is retained in the panel per
Note drawing for the current scene session, and is discarded on scene change or
panel destruction. Saving ink with the scene remains a separate extension of
the Note Level. macOS and Linux gain no new command or recognition dependency.

Recognition runs on a dedicated thread, with COM objects confined to that
thread. The newest pending stroke snapshot replaces older pending work. Revision
checks discard late results, and the target's scene, Xsheet, column, level-frame
identity, lock and playback state are checked again before transferring text.
An active Windows recognition call completes before its worker can shut down.

Portable surface tests and an optional Windows recognizer smoke test:

```sh
cmake -S tests/handwriting -B build/handwriting -DCMAKE_PREFIX_PATH=<Qt5-path>
cmake --build build/handwriting --config Release
ctest --test-dir build/handwriting -C Release --output-on-failure
```

The Windows smoke test skips if a handwriting language component is unavailable.
It exercises the real API without asserting a spelling for synthetic ink.

Application checks before merging:

- Transcribe into a Note drawing whose level-frame number differs from its
  Xsheet row; check normal save/reopen, undo/redo, and a held exposure.
- Switch frames or scenes during recognition; confirm no result reaches the
  previous destination. Check pinned and following destinations and playback.
- Edit a note outside the panel or undo a transfer; confirm automatic transfer
  pauses. Check locked columns and replaced/deleted target cells.
- Check physical pen input with the user's Windows Ink/tablet configuration and
  both Xsheet and Timeline layouts.

The feature remains **Do Not Merge** pending integrated Windows application
validation. The preceding Python demo established local recognition on the
user's machine; it does not establish these C++ application checks.
