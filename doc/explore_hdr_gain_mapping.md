# Explore HDR gain mapping

This exploration adds **HDR Gain Map** and **Apply HDR Gain Map** under
**Add FX → Image Adjust**. They use OpenToonz's floating-point raster and
linear-color conversion processes. No FFmpeg, external library, AI model, or
particular GPU is required.

The starting point is Shun Iwasawa's floating-point / linear rendering work in
[PR #4700](https://github.com/opentoonz/opentoonz/pull/4700). The existing
Flipbook gain controls inspect HDR values without changing the rendered image.
These new FX generate and apply a spatial gain map inside the scene itself.

## First experiment

1. In Output/Preview Settings select **32bit Floating point** for Channel Width.
   Enable **Linear Color Space** for a consistent linear blending workflow.
   Use effects that preserve floating-point values; an earlier clamping FX can
   already have discarded highlight detail.
2. Prepare matching SDR and HDR branches of the same artwork. Keep their
   placement and alpha coverage aligned. For a simple experiment, use the same
   drawing in both branches and brighten one through **Adjust Exposure Iwa**.
   A brighter HDR branch can contain channel values above 1.
3. Add **HDR Gain Map**. Connect the ordinary rendition to **SDR** and the
   brighter rendition to **HDR**. Preview its output to inspect the map.
4. Add **Apply HDR Gain Map**. Connect the original SDR branch to **SDR** and
   the generated map to **Gain Map**. Keep the four Map Encoding parameters
   identical in both FX.
5. At **Strength = 0%** the result is SDR. At **100%**, RGB mode reconstructs
   the HDR colors within the selected gain range, retaining the SDR alpha.
   Intermediate strengths interpolate exposure in stops, rather than linearly
   dissolving the images.
6. Inspect the reconstructed result in Flipbook. Reduce its existing gain to
   reveal highlight values above ordinary display white. Compare against the
   original HDR branch at the same Flipbook gain.

Using a constant exposure adjustment makes an approximately uniform map away
from black offsets. To investigate local gains, brighten a flame, reflection,
or other selected area in the HDR branch while preserving the surrounding art.

## Encoding controls

| Control | Meaning |
| --- | --- |
| Minimum / Maximum Gain (stops) | Fixed encoded range; defaults are -8 and +8. Values outside it are clipped. Minimum must be below Maximum. |
| Map Encoding Gamma | Distribution of normalized map values, independent of the scene's Color Space Gamma; defaults to 1. |
| Black Offset | Added to both straight linear colors before division to stabilize near-black values; defaults to 1/64. |
| Mode | RGB stores separate gains. Luminance uses working-RGB BT.709 luminance weights. Max RGB compares the largest components. |
| Strength (%) | Fraction of the decoded exposure adjustment applied by Apply HDR Gain Map; defaults to 100%. |

RGB mode supports different gains in the three channels. The scalar modes
apply a common gain to all channels and need not reconstruct color changes
between the original renditions exactly.

For straight linear channel values S and H, offset o, range [a,b], and map
gamma g, the normalized linear map is:

```
stops = log2((H + o) / (S + o))
map = clamp((stops - a) / (b - a), 0, 1) ^ g
```

Application at weight w reverses the encoding:

```
stops = a + map ^ (1/g) * (b - a)
result = max(0, (S + o) * 2 ^ (w * stops) - o)
```

Calculations use straight colors and then restore premultiplication. With the
default symmetric range and gamma 1, neutral gain is **0.5 in linear space**;
its displayed brightness depends on the normal viewing color conversion.

Negative/nonfinite source RGB values are treated as zero. An uncovered HDR
pixel generates neutral gain; an uncovered map pixel leaves SDR unchanged.
Generation requires both ports, while application with no map passes SDR
through. The map cannot create new alpha coverage or restore already clipped
source detail. Different geometry between the inputs is compared as positioned
in the scene; it is not automatically registered or resized.

## Saving and reuse

FX parameters, connections, and animation are saved through the normal scene
process. Keep encoding values consistent throughout a sequence and match any
animated encoding parameters between the generator and application FX.

Render either the map or the reconstructed output through normal EXR output.
Prefer floating-point EXR for maps and HDR masters. Reimport with the matching
EXR Color Space Gamma so that the native linear conversion restores the same
map values; avoid color adjustments, lossy video formats, or extra LUTs on the
map branch. A map saved separately does **not** embed its four encoding
parameters: retain the scene/preset or record them alongside the map.

This exploration implements native generation, inspection, and application.
It does not write ISO 21496-1/Ultra HDR JPEG metadata, import an embedded JPEG
gain map, derive absolute display luminance, or enable HDR monitor output.
Strength previews exposure adaptation; it is not a calibrated display-headroom
simulation. Those are separate follow-up integrations.
