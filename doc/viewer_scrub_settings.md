# Viewer Scrub Settings

Open **Viewer Scrub Settings...** with the gear beside the viewer's keyframe
controls, or from the viewer's right-click menu. The small floating window can
be moved beside the artwork and remains open while drawing or scrubbing.

- Drag the horizontal strip left or right to move through frames. Clicking
  starts a relative gesture; it does not jump to an absolute timeline position.
- Hold the existing **Viewer Scrub** shortcut and drag in the viewer to use the
  same options over the artwork. The assigned shortcut appears in the window.
- **Step by** selects **Frames** (the default) or **Drawing changes** in the
  current column for both the viewer gesture and strip. Drawing mode skips held
  exposures: a drawing on frame 1 held through frame 8 jumps directly to the
  next exposure on frame 9. Backward movement visits the start of the previous
  exposure. Reused drawings, changes of level, and intentional blank gaps are
  separate stops. Level editing visits available drawings in either mode.
- **Viewer step in pixels** and **Strip step in pixels** adjust sensitivity
  immediately. Smaller values move through more frames or drawing changes for
  the same distance, according to **Step by**.
  Defaults are 20 pixels per frame in the viewer and 2 in the strip. These use
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
Drawing mode caches exposure boundaries when a gesture starts; changing the
current column or editing the Xsheet invalidates that cache and ends the gesture.

## Validation

The movement regression test is independent of Qt and the full application:

```sh
cmake -S toonz/sources/toonz/tests -B build/viewerscrub-tests -DBUILD_SPLASHIMAGE_TEST=OFF
cmake --build build/viewerscrub-tests
ctest --test-dir build/viewerscrub-tests --output-on-failure
```

Application checks: open the controls in Viewer and Combo Viewer; test a long
scene, held cells, sparse level drawing IDs, range wrapping in both directions,
return on mouse/key release, Escape, focus loss, and closing or changing Rooms.
Check pen input and mixed-DPI displays on the target platforms. Verify Preview
and sound toggles retain their existing behavior, then draw again after each
scrub to check that no unintended stroke is created.

Design reference: [OpenToonz discussion #7239](https://github.com/opentoonz/opentoonz/discussions/7239),
with Rodney Baker's floating settings window and compact strip request.
Implementation assisted by OpenAI Codex.
