# GLB Model FX — framework

Add **Render > GLB Model** from the FX Schematic. This is a zerary (source)
effect with no input port and a standard raster output. This first stage only
provides the node and its scene-owned settings: **it does not load or render
GLB data**. An empty preview and transparent render are expected, even after
choosing a file.

## Settings

| Page | Controls | Stored meaning |
| --- | --- | --- |
| Model | GLB File, Browse, Clear | File reference only; Browse filters `.glb` files. Clear removes the reference, not the file. |
| Model | Position X/Y/Z; Rotation X/Y/Z; Scale | Instance translation in model units, rotation in degrees, uniform scale in percent (100 = unchanged). No model data is edited. |
| Camera | Projection; Camera Distance | Orthographic or perspective; distance in model units. |
| Camera | Orthographic Height / Vertical Field of View | Height in model units for orthographic projection, or vertical angle in degrees for perspective. Only the applicable field is shown. |
| Camera | Near Clip; Far Clip | Positive distances in model units. Rendering will require Far Clip greater than Near Clip; this stage stores them without evaluating a camera. |
| Appearance | Display Style; Lighting | Solid or wireframe. Solid mode exposes Unlit or Headlight lighting. |

All numeric fields use ordinary FX keyframing; selectors and the file reference
are non-animated. The standard FX controls provide undo/redo and parameter
reset. Values are retained by scene save/reopen, FX presets and cloning. File
selection stores an absolute path; manual path text is stored as entered. There
is no dependency copying, path resolution, missing-file validation, reload,
model export, embedded animation playback, or renderer dependency in this stage.

## Framework acceptance gate

1. Add GLB Model; verify the node has no input port and opens FX Settings.
2. Browse to a GLB whose path includes spaces and non-ASCII characters. Cancel
   another Browse operation and verify the reference is unchanged. Clear it;
   undo/redo the change and verify the original file remains untouched.
3. Edit and keyframe numeric settings. Switch projection and display style;
   verify the appropriate fields appear without losing hidden values, including
   after undo/redo and after switching between two GLB nodes with different modes.
4. Save/reopen the scene, duplicate the FX, and save/load an FX preset. Verify
   the path, selectors, numeric values and keyframes survive. Reset values and
   undo/redo normal field changes.
5. Request preview and render at 8-bit, 16-bit and floating-point precision.
   The output must remain transparent without opening the referenced file.
6. Repeat the UI smoke test on Windows, Linux and macOS before advancing the
   stack. No GLB processing or connection/render integration PR is started
   until this framework is accepted.

The optional `BUILD_GLB_FX_TESTS=ON` CMake switch builds `glbmodelfx_test`, a
focused regression executable using the production FX source. Windows CI runs
it before the application build. UI checks above still require the application.
