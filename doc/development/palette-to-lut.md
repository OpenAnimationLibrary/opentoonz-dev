# Palette to LUT Generator

Right-click in the Palette window's style area or page tabs and choose
**Create 3D LUT from Palette Keys...**. The same style-area action is available
in the Studio Palette viewer. Export reads a snapshot, including unsaved edits;
it does not change drawings, the scene frame, palette keys, or palette locking.

1. Use the current palette, or **Reference Palette...** to open any `.tpl` file.
2. Set the source and target frames (defaults: 1 and 2).
3. Check the source and target swatches in the style table. A style's key dropdown
   can override either global frame using one of that style's saved keys.
4. Set **Tolerance** globally or individually. This is Euclidean RGB distance in
   0–255 units, with smooth falloff. A higher value affects more neighboring
   shades; 25.5 corresponds to radius 0.1 in normalized RGB.
5. Choose a 33 or 65 point grid. **Preview** shows the actual baked result for
   each usable style, hue/value ramps, grayscale and the maximum anchor error.
6. **Export LUT...** writes a standard `.cube` with overwrite confirmation and
   atomic replacement. Use it with the existing LUT effects or Bake command.

## Style rules

- Only opaque solid-color styles contribute RGB instructions initially.
- A checked style with explicit keys at both chosen frames maps its source RGB
  to its target RGB. Extra keys do not participate.
- Unchecked styles, styles missing either key and identical keyed colors act as
  preservation constraints at their evaluated source RGB.
- Transparent style 0, partially transparent colors and complex styles are
  skipped and identified in the table. LUTs cannot encode palette alpha changes.
- Deleted/unpaged style slots are excluded.
- Duplicate source RGB values with different targets are rejected, including a
  changed style conflicting with a preservation constraint. Identical duplicates
  merge using the larger tolerance.

The transform starts from identity and uses local RGB displacement with smooth
falloff. Trilinear constraints enforce changed and preserved colors on the baked
grid. If the grid cannot resolve nearby competing mappings within 0.25/255 per
channel, generation fails with guidance to increase resolution or revise them.
Even zero tolerance needs a grid-sized neighborhood: a finite interpolated LUT
cannot represent an isolated discontinuous exact-color replacement. The influence
can extend by one lattice cell around a tolerance boundary.

Palette RGB values are used in their existing encoding without an implicit color
space conversion. Apply the LUT to matching RGB encoding. This is a color-based
transform: it does not retain style IDs or isolate characters/layers.

## Shared OTLUT implementation

The source snapshot in `thirdparty/otlut` is BSD-licensed and attributed to
OpenAnimationLibrary/otlut PR #1. `otlut_core` is linked into the Palette dialog
and the bundled standalone executable. Palette fitting and paired-image fitting
share the internal LUT representation and writers but use separate policies:
palette fitting is local and constrained; image fitting fills unsampled cells.

The File and Scene Browsers use a common cancellable helper service, validate
generated output with OpenToonz's production LUT reader and atomically commit it.
The bundled executable remains usable independently for image pairs and explicit
color pairs; Preferences can still select a different executable.

```text
otlut --source original.png --target graded.png --output look.cube --size 33
otlut --color-pairs pairs.txt --output look.cube --size 33
```

Color-pair files are UTF-8 text, one whitespace-separated row per color:

```text
# source R G B   target R G B   tolerance   optional label
1 0 0           0.0352941176 1 0   0.1     red_to_green
0 0 0           0 0 0             0.1     preserve_black
```

The seven numeric values use normalized 0–1 coordinates. Comment and blank lines
are accepted; labels describe conflicts. The native adapter owns TPL parsing,
while this format and the core API allow other callers to supply correspondences.

No build-time download is needed. The pinned stb decoder and its license notice
are included. Windows packages include `otlut.exe` and the license notices; macOS
bundles copy the helper into Contents/MacOS; Linux installs it alongside OpenToonz.

## Validation

Standalone checks:

```text
cmake -S toonz/sources/otlut -B otlut-tests -DOTLUT_BUILD_TESTS=ON
cmake --build otlut-tests --config Release
ctest --test-dir otlut-tests -C Release --output-on-failure
```

Tests cover the supplied `example_keys.tpl`, its zero-based stored frame numbers,
red-to-green mapping, black preservation, tolerance/falloff, zero tolerance,
conflicting mappings, unresolvable nearby colors, cancellation, cube ordering,
image-pair regression and preservation of an existing output on failure.
The supplied `simple_palette_cycle.tpl` regression covers three cyclic mappings
near grid planes at both supported sizes, including saturated target channels.
When built inside OpenToonz with Qt, an additional production-reader test checks
palette, identity and asymmetric transforms in `.cube` and `.3dl` formats.
The Qt task test verifies error recovery and repeated generation followed by
nested dialog event loops, with no polling of a consumed future.
The Windows build runs these checks; standalone CI covers Windows, Linux and macOS.

Manual application checks: verify both context-menu locations, referenced Studio
palettes, unsaved edits, per-style key overrides, small-screen table scrolling,
preview, cancellation, and overwrite confirmation. Confirm image-pair commands
still produce and load a nonidentity LUT using the bundled helper.
