# Viewer Scrub Settings

Open **Viewer Scrub Settings...** with the gear beside the viewer's keyframe
controls, or from the viewer's right-click menu. The small floating window can
be moved beside the artwork and remains open while drawing or scrubbing.

- Drag the strip horizontally, vertically, or diagonally to scrub. The strip
  measures movement from where you press.
- Hold the existing **Viewer Scrub** shortcut and drag in the viewer to use the
  same options over the artwork. The assigned shortcut appears in the window.
- **Step by** selects **Frames** (the default) or **Drawing changes** for
  drags that start closer to horizontal or vertical. Drags that start closer
  to a 45-degree diagonal use the other step type. This applies in both the
  viewer and the strip. Right, down, and rightward diagonals move forward.
  The direction and step type lock after a short initial move, so you can
  reverse without switching modes.
- Drawing changes skip holds in the current column. A drawing on frame 1 held
  through frame 8 jumps directly to the next exposure on frame 9. Backward
  movement visits the start of the previous exposure. Reused drawings, changes
  of level, and intentional blank gaps are separate stops. With no current
  column, scrubbing steps by frame. Level editing visits available drawings.
- **Viewer step in pixels** and **Strip step in pixels** adjust sensitivity
  immediately. Smaller values move through more frames or drawing changes for
  the same distance. The defaults are 20 pixels per step in the viewer and
  2 in the strip. These use
  logical screen pixels, independent of artwork zoom and display pixel ratio.
- **Return to starting frame on release** enables temporary inspection.
  Escape during a drag always cancels back to its starting frame.
- **Use playback range** uses the scene's existing playback markers. Level
  editing uses its available drawings, including nonconsecutive drawing IDs.
- **Loop between range limits** wraps manual dragging at the limits; otherwise
  movement stops there. This does not start automatic playback.
- **Reset** restores the defaults. Settings are saved with user preferences.

Scrubbing is disabled while playback or a drawing stroke is active. Scene,
sub-Xsheet, level-editing context, and active-viewer changes invalidate an
unfinished gesture so releasing it cannot restore a frame in another context.
Drawing changes cache exposure boundaries when the step type is selected for
the gesture. Changing the current column or editing the Xsheet invalidates that
cache and ends the gesture.

## Validation

The movement regression test is independent of Qt and the full application:

```sh
cmake -S toonz/sources/toonz/tests -B build/viewerscrub-tests -DBUILD_SPLASHIMAGE_TEST=OFF
cmake --build build/viewerscrub-tests
ctest --test-dir build/viewerscrub-tests --output-on-failure
```

Open the controls in Viewer and Combo Viewer. Test a long scene, held cells,
sparse level drawing IDs, and range wrapping in both directions.
Try shallow and steep diagonal drags, then turn or reverse before releasing to
check that the initial direction stays locked. Also test return on mouse/key
release, Escape, focus loss, and closing or changing Rooms.
Check pen input and mixed-DPI displays on the target platforms. Verify Preview
and sound toggles retain their existing behavior, then draw again after each
scrub to check that no unintended stroke is created.

Design reference: [OpenToonz discussion #7239](https://github.com/opentoonz/opentoonz/discussions/7239),
with Rodney Baker's floating settings window and compact strip request.
Implementation assisted by OpenAI Codex.
