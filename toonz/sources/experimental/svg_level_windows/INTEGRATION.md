# Production integration notes

The branch now contains an enabled Windows-only production path for the first experimental SVG Level milestone.

## Integrated files

- `toonz/sources/toonz/svglevelloader.h`
- `toonz/sources/toonz/svglevelloader.cpp`
- `toonz/sources/toonzlib/svglevelloadintercept.cpp`
- `toonz/sources/include/toonz/toonzscene.h`
- `toonz/sources/include/toonz/txshleveltypes.h`

`toonz/sources/toonzlib/CMakeLists.txt` builds the loader and intercept only when `BUILD_TARGET_WIN` is enabled, and adds the required Qt SVG and Qt Widgets libraries.

## Loading flow

1. A normal OpenToonz level-loading command calls a convenience overload of `ToonzScene::loadLevel()`.
2. On Windows, an SVG introduced by the user is intercepted before the established four-argument loader implementation runs.
3. OpenToonz asks whether to:
   - **Open as SVG Level (Experimental)**
   - **Convert to Toonz Vector Level**
   - **Cancel**
4. The conversion choice delegates unchanged to the existing four-argument SVG-to-PLI loader path.
5. The experimental choice keeps the `.svg` source path and creates one read-only `SVG_XSHLEVEL` frame.
6. Qt SVG rasterizes the complete frame to a transparent premultiplied `TRasterImage`.
7. The existing non-PLI branch of `TLevelColumnFx` displays, transforms, caches, and composites the generated raster with other columns.

Scene resource loading remains non-interactive. The prompt is skipped while `ToonzScene` is loading an existing scene, and headless Qt applications retain the existing SVG-to-PLI behavior.

## Expected first milestone

- Opening an SVG presents the new handling choice.
- Selecting the experimental route adds an SVG Level to the Scene Cast and exposes frame 1 in a standard level column.
- The SVG should appear in the Viewer and camera preview.
- Static SVG text should be rendered using fonts available to Qt on the Windows system.
- Column position, scale, rotation, opacity, stacking, and downstream raster FX should operate on the generated frame.
- The SVG source remains unchanged and the level is marked read-only.
- Drawing tools are not yet deliberately capability-gated. The current image-type fallback should prevent normal drawing-tool editing, but dedicated SVG tool rules remain a follow-up.

## Suggested Windows test

1. Build the branch with the normal OpenToonz Windows configuration.
2. Start a new scene.
3. Load `toonz/sources/experimental/svg_level_windows/testdata/basic_composite.svg`.
4. Choose **Open as SVG Level (Experimental)**.
5. Confirm that the gradient, shapes, transparency, transform, and two text elements appear.
6. Add a raster, Toonz Raster, or PLI level in another column and confirm mixed compositing.
7. Animate the SVG column position, scale, and rotation.
8. Reload the same SVG and choose **Convert to Toonz Vector Level** to confirm that the previous conversion route still works.

## Known milestone limitations

- Saving and reopening a scene does not yet restore `SVG_XSHLEVEL`; `TXshSimpleLevel::loadData()` still maps an `.svg` path to PLI.
- The generated frame is cached for the current session. A dedicated SVG image builder for regeneration after cache eviction is still needed.
- SVG sequences are not yet implemented; the experimental route creates frame 1 only.
- The raster resolution currently comes from SVG intrinsic dimensions or its `viewBox`.
- Editing, element selection, copy/paste conversion, SVG source editing, and dynamic rerendering after external file changes are future milestones.
- Cancelling from the experimental handling dialog currently returns through the normal failed-load result path; silent cancellation belongs in a later direct `IoCmd` integration cleanup.
