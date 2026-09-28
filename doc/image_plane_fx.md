# Image Plane FX

Connect an image or raster FX to **Image Plane**, then connect Image Plane to **3D Transformer**. Image Plane centers the source's finite bounding box on a two-sided, camera-facing plane. At the default orthographic view, one source pixel occupies one output pixel. The plane has no visible background: the source's premultiplied alpha defines its silhouette, including antialiased edges and holes.

The source is evaluated once per requested scene at its native pixel coordinates. The plane accepts a finite source bounding box of at most 8192 pixels per side and 16 megapixels in total. An unbounded generator needs a bounded input or crop before this FX. The input's box is centered on the plane; any offset from the scene origin is intentionally removed. Animated raster inputs are evaluated at the requested frame.

3D Transformer supplies position, rotation, and scale before the plane is projected. The first version uses orthographic projection and nearest-neighbor texture samples; camera crossings are culled. Image Plane has no transform controls of its own. The Three-Point Light FX can be connected but does not change the image colors: the image is an unlit surface.

The renderer samples the input's RGBA texture inside two triangles. Zero-alpha texels leave the depth sample empty; there is no colored rectangular backing. Transparent raster data is never converted to an alpha threshold or polygon outline.
