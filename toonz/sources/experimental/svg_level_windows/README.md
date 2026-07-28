# Experimental SVG Level — Windows prototype

This directory contains the standalone rasterization probe used to establish native, read-only SVG Levels in OpenToonz.

The experiment deliberately does **not** convert SVG artwork to PLI. It retains the SVG file as the source and rasterizes a complete transparent RGBA frame for display and compositing.

## Current scope

- Windows only.
- Qt 5 Core, Gui, Svg, and Widgets.
- Loads one SVG document.
- Uses the SVG `viewBox` or intrinsic size to determine its natural frame.
- Rasterizes to transparent premultiplied RGBA.
- Renders static SVG text using fonts available to Qt on Windows.
- Rejects invalid SVG and unreasonable output dimensions.
- Provides the handling choice:
  - **Open as SVG Level (Experimental)**
  - **Convert to Toonz Vector Level**
  - **Cancel**

The production-facing loader is now included in the Windows `toonzlib` target. The standalone probe remains useful for testing Qt SVG rendering independently of the full application.

## Build the standalone probe

From a Visual Studio developer command prompt with Qt 5 available:

```bat
cmake -S toonz\sources\experimental\svg_level_windows ^
      -B build\svg-level-probe ^
      -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64
cmake --build build\svg-level-probe --config Release
```

## Run the standalone probe

Rasterize directly:

```bat
build\svg-level-probe\Release\svg_level_probe.exe input.svg output.png
```

Show the SVG handling choice before rasterization:

```bat
build\svg-level-probe\Release\svg_level_probe.exe input.svg output.png --ask
```

An explicit output size can be supplied:

```bat
build\svg-level-probe\Release\svg_level_probe.exe input.svg output.png 1920 1080
```

## Test in OpenToonz

After building the Windows application target:

1. Start a new scene.
2. Load `testdata/basic_composite.svg`.
3. Select **Open as SVG Level (Experimental)**.
4. Confirm that the SVG appears in the Viewer and that the two text elements render.
5. Transform the column and composite it with another level.
6. Load the SVG again and select **Convert to Toonz Vector Level** to verify the existing PLI route.

See `INTEGRATION.md` for implementation details and the limitations of this milestone.
