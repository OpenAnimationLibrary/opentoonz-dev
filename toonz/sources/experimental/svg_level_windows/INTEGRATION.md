# Production integration notes

The branch now contains the production-facing Windows loader in:

- `toonz/sources/toonz/svglevelloader.h`
- `toonz/sources/toonz/svglevelloader.cpp`

and defines `SVG_XSHLEVEL` in `toonz/sources/include/toonz/txshleveltypes.h`.

## Remaining build-list wiring

Add the files to `toonz/sources/toonz/CMakeLists.txt` only for Windows builds:

```cmake
if(BUILD_TARGET_WIN)
    list(APPEND HEADERS svglevelloader.h)
    list(APPEND SOURCES svglevelloader.cpp)
endif()
```

The OpenToonz Windows target already links `Qt5::Svg`.

## Remaining loader call-site

In `IoCmd::loadResources`, detect `.svg` resources before the normal `scene->loadLevel()` route. Ask once per selected SVG resource:

```cpp
#ifdef _WIN32
if (actualPath.getType() == "svg") {
  switch (SvgLevel::askOpenMode(TApp::instance()->getMainWindow())) {
  case SvgLevel::OpenMode::Cancel:
    return 0;
  case SvgLevel::OpenMode::OpenExperimentalSvgLevel:
    xl = SvgLevel::loadExperimentalLevel(scene, actualPath, levelName);
    break;
  case SvgLevel::OpenMode::ConvertToToonzVector:
    break;  // Continue through the existing scene->loadLevel SVG-to-PLI path.
  }
}
#endif
```

The call should occur in the first-load branch before `scene->loadLevel()`. When `xl` was created by `loadExperimentalLevel()`, skip the existing `scene->loadLevel()` call and continue through normal cast-folder, history, undo, and exposure handling.

## Expected first milestone

- Opening an SVG asks whether to retain it as an experimental SVG Level or convert it to PLI.
- The experimental route creates one `SVG_XSHLEVEL` frame.
- The frame is a premultiplied full-color `TRasterImage` generated from the retained SVG source.
- `TXshColumn::toColumnType()` accepts it because the new level type includes `LEVELCOLUMN_XSHLEVEL`.
- `TLevelColumnFx` follows its existing non-PLI raster branch and composites it with other columns.
- The source level is read-only; drawing-tool gating is a later patch.

## Persistence follow-up

Scene reload still needs an explicit SVG mapping in `TXshSimpleLevel::loadData()` and a Windows-only SVG branch in `TXshSimpleLevel::load()` that regenerates frame 1 from the source. Until that is added, this milestone should be tested in a newly loaded scene without relying on save/reopen.
