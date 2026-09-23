# 3D Transformer FX

Add **3D > 3D Transformer** in the FX Schematic and connect a compatible 3D
source to its `Source` port. The node applies an additional animated model-space
transform without flattening the source to pixels. It can be connected directly
to the scene output or placed before another 3D-aware node such as Three-Point
Light. Multiple 3D Transformer nodes may be chained; they are evaluated from the
source outward.

## Parameters

| Control | Behavior |
| --- | --- |
| Canvas Gizmo | Selects Translate, Rotate, or Scale handles shown by the Edit Tool. This selector is saved but not animated. |
| Position X/Y/Z | Translation in model units. |
| Rotation X/Y/Z | Euler rotation in degrees, applied in X, Y, Z order. |
| Scale X/Y/Z | Independent axis scale in percent. `100` preserves the incoming size. Scale must remain positive. |

All nine transform values use normal OpenToonz FX keyframes and Function Editor
channels. The input model and its GLB file are never modified.

## Canvas gizmo

Select the 3D Transformer node, choose the **Edit Tool**, and choose a Canvas
Gizmo mode in FX Settings:

- **Translate** shows red X, green Y, and blue Z arrows. Drag an axis to change
  only that component. Drag the center square for free XY movement.
- **Rotate** shows separate X, Y, and Z rotation rings. Drag a ring to change its
  corresponding Euler angle.
- **Scale** shows axis boxes for nonuniform scale. Drag the center box for
  uniform proportional scaling of the current X/Y/Z values.

Hold **Alt** for the Edit Tool's existing precision-drag behavior. Hold **Shift**
to snap translation, rotation, or scale to practical increments.

The gizmo uses the conventional X-red, Y-green, Z-blue axis colors. The X and Y
rotation rings are flattened so all three axes remain selectable from the 2D
canvas; a single 2D angle control would be unable to distinguish true X, Y and Z
rotation. Translation and scale reuse the established FX-gadget undo, selection,
keyframe and picking path. The new rotation rings are the only new control shape.

The canvas overlay is intentionally the first interaction surface. FX swatch
rendering reflects every transform, but the swatch does not yet host Edit Tool
overlays or input routing. Adding swatch-side manipulation belongs in a shared
viewer-control follow-up rather than a node-specific event path.

Position X and Y place the gizmo using the GLB Model default orthographic mapping
(100 canvas units per model unit), so it initially tracks a default GLB source.
The actual transform remains model-space and is exact. A source with a changed
camera, projection, or its own upstream translation can render away from the
overlay; sharing projected-origin metadata through the general 3D source
contract is a follow-up for camera-aware gizmo placement.

## Transform order and rendering

The GLB Model node's transform is evaluated first. Each downstream 3D Transformer
then applies scale, X/Y/Z rotation, and translation. The camera transform,
clipping, projection, face lighting, depth testing, antialiasing, and raster
output occur afterward. This preserves correct silhouettes and face normals for
nonuniform scaling and allows Three-Point Light to shade the transformed model.

The ordered transform stack participates in the GLB render cache identity.
Clones, scene persistence, presets, preview, final render, 8/16/float output and
render cancellation use the normal FX framework paths.

## Application acceptance

1. Connect GLB Model to 3D Transformer and the transformer to the output. Confirm
   that an unconnected transformer renders transparent and rejects a normal
   raster FX on its Source port.
2. Select the transformer and Edit Tool. Exercise each colored Translate,
   Rotate, and Scale handle, the XY translate square, and uniform-scale square.
   Confirm FX Settings update during the drag and undo/redo restores values.
3. Key all nine transform channels at two frames. Scrub and render between them;
   verify the canvas, FX swatch, preview, and final output agree.
4. Chain two transformers. Confirm the source-side node is evaluated first and
   changing their order produces the expected different result.
5. Connect the transformer to Three-Point Light. Rotate and nonuniformly scale the
   model; verify lighting follows the transformed face geometry.
6. Save/reopen, duplicate the node, save/load a preset, and test 8-bit, 16-bit and
   floating-point output. Confirm the GLB file remains unchanged.
