# 3D LUT Bake FX

Add **Image Adjust > 3D LUT Bake** in the FX Schematic. Connect the completed
composition to **Source**, and connect this FX to the output. Use **Browse...**
to select a `.cube` or `.3dl`, or **Use Preferences LUT** to copy the LUT path
configured for the current monitor in Preferences. An empty path passes through.
The standard FX Intensity control can blend the effect with its source.

The path is saved in the scene. Changing or disabling the Preferences LUT later
does not change the FX. The file must remain available at the saved location on
every rendering machine; this first version does not package LUT files or resolve
project aliases. Copy the LUT and reselect its path when moving a scene.

## Comparing the look

1. Preview the finished, opaque composition with its look LUT enabled in Preferences.
2. Assign that file to the Bake FX and disable the Preferences display LUT using
   **Toggle Active LUT**. Otherwise the look is applied twice in the Viewer.
3. Render the output. Inspect the rendered file with the same display LUT disabled.

A LUT that compensates for a particular physical monitor is not normally a
portable creative look. Select a LUT intended for the delivered image. This FX
applies a forward transform; it cannot recover colors clipped by a baked LUT.
Keep the original scene/source for an unbaked version.

## Processing

The FX shares its parser and trilinear sampler with Preferences. Supported formats
are the existing 3DMESH `.3dl` variant and 3D `.cube` tables, with 2–129 points per
axis. Cube domains and `LUT_3D_INPUT_RANGE` are honored. 1D/shaper LUTs and automatic
log/gamut/OCIO conversions are not supported.

The LUT is evaluated in OpenToonz's nonlinear RGB, matching the display path.
When linear rendering is enabled, the existing FX framework converts around this
node using the scene color-space gamma. There is no additional gamma parameter.
Use a LUT authored for those input values. This is an SDR look bake: transformed
RGB is clipped to 0–1 consistently for 8-bit, 16-bit and floating-point rasters.
Floating-point rendering avoids intermediate 8-bit quantization.

RGB is unpremultiplied before lookup and premultiplied afterward; alpha is
preserved. Zero-alpha pixels are cleared. To match the displayed background and
antialiased edges, composite the actual delivery background before the LUT.
Applying a nonlinear LUT before compositing a transparent export onto a new
background can give a different result from transforming the finished composite.

LUT data is shared as an immutable snapshot among concurrent tiles. File path,
modification time, size and availability participate in render-cache identity;
normal file edits therefore invalidate prior results on the next evaluation.
Do not edit a LUT during an active render. Changes that deliberately preserve both
file size and modification time require touching/resaving the file. An unavailable
or malformed assigned file produces a render exception with the FX identifier
and path instead of silently using an old LUT.

## Regression tests

Configure with `-DBUILD_LUT3D_TESTS=ON` and build `lut3dbake_test`. Run it with the
normal OpenToonz runtime libraries on the search path. Windows CI runs this test
after creating its application package and removes the test executable before
publishing the package.

The executable compiles the production FX source and tests analytic nonlinear
color transforms, cube and 3dl channel ordering, interpolation, domains, malformed
files, 8/16-bit and float processing, partial/zero alpha, bypass, SDR clipping,
file-change cache invalidation, missing-file failure, concurrent tiles, Unicode
paths, and FX serialization. It does not exercise interactive file dialogs or
compare the OpenGL display to an actual output encode; those remain manual checks.
