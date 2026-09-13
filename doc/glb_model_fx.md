# GLB Model FX

Add **Render > GLB Model** from the FX Schematic, choose a `.glb` file, and connect
the output to the scene output or another raster FX. This is a zerary source
with no input port. It renders opaque grayscale or material-colored surfaces over a
transparent background, using the same CPU renderer for preview and final output.
The source model is never edited or written.

For a first test, select **Solid** and **Headlight**. If the model is outside the
view, adjust its Position, Camera Distance, and Orthographic Height. There is no
automatic recentering or fitting: original model coordinates remain meaningful.

## Controls

| Page | Controls | Behavior |
| --- | --- | --- |
| Model | GLB File, Browse, Clear | Browse stores an absolute path. Clear restores transparent output and does not delete the model. |
| Model | Position X/Y/Z | Translation in model units, after scaling and rotation. |
| Model | Rotation X/Y/Z | Degrees, applied around the model origin in X, then Y, then Z order. |
| Model | Scale | Uniform instance scale in percent; 100 preserves model size. |
| Camera | Projection | Orthographic or perspective. The camera is on +Z, looking toward the origin, with +Y up. |
| Camera | Camera Distance | Distance from the origin in model units. Move the camera beyond the model's front surface. |
| Camera | Orthographic Height | Model-space height represented by the fixed 1,000-unit FX image plane. Larger values make the model smaller. |
| Camera | Vertical Field of View | Perspective view angle in degrees, on the same fixed image plane. |
| Camera | Near Clip, Far Clip | Positive distances from the camera. Far Clip must exceed Near Clip. Crossing surfaces are clipped before projection. |
| Appearance | Solid, Wireframe | Filled surfaces or visible triangle edges, with depth testing. Hidden edges are suppressed. |
| Appearance | Color Mode | Grayscale (the existing default) or Material Colors. |
| Appearance | Unlit, Headlight | Flat colors or faceted shading from a light at the camera. Wireframe is unlit. |
| Materials | Material selector, color, keyframe diamond | Lists the chosen GLB's materials; edit or animate the selected material's RGB color. |

The fixed image plane is centered at the FX origin, independent of output
resolution, preview zoom, camera-box size, and tile boundaries. OpenToonz's
ordinary 2D affine is applied after 3D projection, so column/stage transforms,
preview zoom and output scaling work normally. Orthographic Height is not an
automatic fit to the current scene camera frame.

Numeric controls retain ordinary FX keyframing. File and mode selectors are
non-animated. Scene save/reopen, FX presets, cloning, reset, and undo/redo retain
the original parameter names and values from the framework stage.

## Rendering scope

- Indexed and non-indexed triangle lists, strips and fans, including shared
  mesh instances and node transforms. The declared default scene is used; if
  none is declared, the first scene is used with a diagnostic warning.
- Opaque, two-sided base geometry, with optional material base colors. Textures, material
  alpha, emissive properties, vertex colors and imported normals are not shaded.
  Headlight uses geometric face normals; it is intentionally faceted.
- No skeletal animation, morph deformation, embedded animation playback, or
  imported cameras/lights. Existing loader warnings remain in the application
  log. The FX Settings labels also state the rendering limitations.
- Point and line primitives are skipped with a diagnostic warning. A scene
  without triangle surfaces, or entirely outside the clip range, is transparent.
- Four coverage/depth samples per pixel produce premultiplied antialiased
  RGB output at 8-bit, 16-bit, and floating-point precision. Normal
  downstream FX and Over compositing can use the resulting raster.

## Material colors and animation

Choose **Appearance > Color Mode > Material Colors**, then open **Materials**.
The selector lists the GLB's material names, with numbered labels to distinguish
duplicates. Unnamed entries use `Material N`. Geometry without an assigned
material uses an additional white `Default material`. All parts sharing one
material use the same color override.

Select a material, adjust its color, and click the diamond to set a key. Move to
another frame, change the color, and click the diamond again. These are native
OpenToonz color parameters: the FX interpolates between keys, and color edits and
key changes support undo/redo. As with other animated FX controls, changing a
color between existing keys previews the change until you set a key. The FX
dialog and Function Editor share the same curves. After a material is edited or
keyed in the FX dialog, it appears in the Function Editor's FX listing under
**Materials > material name > Red, Green, Blue**. The listing updates as overrides
are created; untouched materials do not create scene parameters merely by being
viewed. Enable **Show All Channels** to include unkeyed RGB curves.

The channel groups use parameter identity rather than material names or row
positions, preserving selection and active curves when another material is
added. Retained overrides from a replaced or unavailable GLB are labeled
`Inactive material N`; restoring the original file restores their active names.
Reset removes their listing and clears stale curve selections. Dynamic material
paths are not yet registered as expression-reference names; no unsupported
reference is advertised by these channels.

The list is read-only discovery; just opening it does not add parameters to the
scene. The first edit stores a scene-owned override. Scene/preset persistence
and FX cloning retain its color curves. The source GLB remains untouched.

Identity uses the SHA-256 of the GLB contents plus the material index, rather than
the name or list position alone. Moving an unchanged GLB keeps its overrides.
Changing its contents creates new material identities: prior overrides remain
stored but inactive, with a notice in the editor. Restoring the original file
contents restores those associations and animation. This conservative version
does not automatically migrate colors between edited/re-exported models.

Base-color factors are linear in glTF and are converted to sRGB for display.
Overrides use OpenToonz's native RGB controls; interpolation is sampled at full
parameter precision for 16-bit/float rendering. Headlight modulation occurs in
linear light in material mode. No textures, metallic/specular response, emissive
shading, vertex colors, or material transparency are evaluated. A textured model
may therefore look quite different from its textured appearance in another app.

## Files, errors and memory

The loader remains [read-only](glb_loader.md). Missing, malformed or unsupported
files and invalid camera settings produce a render error with the FX identifier,
reason and source path. An empty file field is intentionally transparent.

Only absolute or ordinary filesystem-relative paths are supported. Relative
paths use the application's working directory; project aliases and dependency
collection are not implemented. Use Browse for an unambiguous saved reference.

Immutable loaded data and the most recently projected frame are shared by
render clones. A mutex protects preparation; rasterization runs without holding
that mutex. Changing file path, modification time, size or availability
invalidates the loader and raster cache keys on the next render request.
Replacing a file while preserving both its timestamp and size is not detected;
use a different filename in that case. There is no background
file watcher, so request a new preview after externally changing the source.

Projected geometry has a separate 256 MiB budget, including vector growth.
Temporary tile data is also bounded; the FX reports its memory needs to the
render scheduler and processes large direct requests in row bands. These are
resource limits, not a restriction to simple models. Render cancellation is
checked during geometry preparation and rasterization.

## Test without OpenToonz

The **GLB Loader and Renderer** workflow builds and tests the standalone tools
on Windows, Linux and macOS. Its `glb-inspect-<platform>` artifacts contain both
`glb_inspect` and `glb_render`. The latter uses the production renderer to write
a 512×512 grayscale TGA with alpha:

```text
glb_render.exe "F:\Models\example.glb" "F:\Renders\example.tga" 10 10
```

The last two arguments are optional Orthographic Height and Camera Distance.
Append `1` after them to render material colors, for example:
`glb_render.exe model.glb colors.tga 10 10 1`.
This utility uses Headlight and zero instance rotation/translation. It refuses
to overwrite an existing output. On Linux/macOS, run `chmod +x glb_render`
after extracting and invoke it as `./glb_render ...`.

The standalone CMake build documented for the loader also runs the renderer
regressions. Windows application CI tests the actual FX, persistence, source
preservation, file replacement, grayscale/RGB precision and Over compositing, both
before and after packaging.
It also exercises the real material selector and native color/keyframe editor,
undo/redo, interpolation, clone/preset persistence and safe model replacement.
Channel-tree tests verify that RGB entries reference the very same curves,
edits reach the renderer and color swatch, additions preserve selection, and
model replacement/restoration and reset refresh the existing listing safely.

## Application acceptance

1. Add GLB Model and verify zero inputs. Browse to a model using a path with
   spaces/non-ASCII characters. Connect the source through Over and a color FX
   to the output; verify a visible model and transparent background.
2. Test Solid/Headlight, Unlit and Wireframe. Switch projection; edit and animate
   each transform and camera control. Check near/far crossings and render an
   animation of the FX transforms. Model-file animation is not evaluated.
3. Compare FX swatch, scene preview and saved output at 8/16/float precision.
   Change output resolution, preview shrink/zoom, and the FX column transform.
   Verify geometry does not shift or develop tile seams.
4. Save/reopen the scene, duplicate the FX, and save/load its preset. Verify all
   settings survive. Clear, undo and redo the path; cancel Browse without
   changing the selection. Check that the source GLB remains unchanged.
5. Try a missing/invalid file and invalid clip distances; verify useful render
   errors. Replace the model externally and request another preview; verify
   the new file revision appears. Cancel a large render and retry it.
6. Enable Material Colors. Select several materials, change their colors and
   animate one from green to blue. Check the swatch, preview and rendered frames.
   Undo/redo edits and key changes; save/reopen the scene and its FX preset.
7. Replace the GLB with a different model. Check that the list changes and old
   overrides stay inactive. Restore the original GLB and verify its animation
   returns. Test unnamed and duplicate material names and shared materials.

These UI checks complement the automated tests; automated pixel tests do not
claim an interactive UI acceptance pass on every platform.
