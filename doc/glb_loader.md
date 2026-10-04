# Read-only GLB loader

The `otglb` library and `glb_inspect` utility load owned GLB data. The
[GLB Model FX](glb_model_fx.md) uses the loader to render opaque grayscale or
material-colored base geometry. Loading remains independent of OpenToonz, image
decoding and rendering. Retaining animation data does **not** enable playback.

## Try a model

Download the `glb-inspect-<platform>` artifact from the **GLB Loader and Renderer** workflow.
Extract it and run the utility from a terminal:

```text
glb_inspect.exe "F:\Models\example.glb"
```

On Linux/macOS, first run `chmod +x glb_inspect` after extracting the artifact,
then use `./glb_inspect /path/to/example.glb`. The utility opens the
source read-only and prints JSON to standard output. To save a report, use your
shell's redirection. Unicode paths are supported, including native Windows
wide-character command-line arguments. Windows requires the standard Microsoft
Visual C++ runtime.

The report includes status, diagnostics, model counts, per-scene base-geometry
bounds, loading time, file size, accounted decoded/work storage and parser
allocation peak. `skinData` adds joint-node order, skeleton and inverse-bind
counts. `animationData` adds named clips, key ranges, sampler interpolation/counts
and channel targets. `deformationEvaluated` is explicitly false.
Vertex/triangle counts describe stored mesh primitives, not the number of times
those meshes are instanced. The decoded counter conservatively includes work
arrays used during loading. Memory counters do not measure total process peak
memory. Exit codes are 0 for loaded (including warnings),
1 for failure, and 2 for command-line usage errors.

## Supported data

- GLB 2.0 with its binary buffer and embedded buffer-view images. External
  resources and data URI resources are explicitly unsupported in this stage;
  no external files or URLs are followed.
- Indexed and non-indexed geometry, including interleaved, normalized and sparse
  attributes. Primitive modes and original index order are retained. Triangle
  counts account for triangles, strips and fans; other topology is identified.
- Mesh-local positions, normals, UVs and colors; mesh instances remain separate
  from geometry. No recentering, remeshing or coordinate-system conversion.
- Named scenes, nodes, mesh/material associations, local/world transforms, and
  bounds computed from base geometry. The declared default scene is preserved;
  a missing default is not an instruction to merge all scenes.
- Core metallic/roughness material factors, alpha mode, double-sided flag,
  texture references, texture coordinates, sampler settings, and embedded image
  bytes. Images are not decoded by the loader.
- `KHR_materials_unlit`, `KHR_texture_transform` and `KHR_mesh_quantization`.
- Authored local translation/rotation/scale with defaults for missing components;
  matrix-authored nodes remain flagged and are not decomposed.
- Owned skins: names, node-to-skin index, joint-node tables, optional skeleton
  root, and column-major inverse bind matrices. Omitted inverse binds become
  identities. Supplied extra matrices are retained as permitted by glTF.
- Owned animations: names (including duplicates/unnamed clips), samplers with
  original second-based key times and decoded values, LINEAR/STEP/CUBICSPLINE
  interpolation, and channel-to-sampler/node/property indices. Shared samplers
  remain shared within their clip. Keys are never reduced or retimed.

Complex models are not rejected for having many parts or polygons. Animation
and skin data are retained but **deformation and playback are not evaluated**.
Reported bounds and existing FX output still describe undeformed base geometry,
not a first animation frame or a posed character. Morph target deltas, default
morph weights, cameras, lights and arbitrary metadata are not retained here.
Unsupported optional extensions produce warnings when core data is usable;
unsupported required extensions (including compression codecs) fail explicitly.
Successful loading does not claim that the renderer supports every feature.

## Owned animation data: contract and limits

`Asset::skins` and `Asset::animations` contain no pointers into cgltf or the source
file. Existing `skinCount`, `animationCount` and `Node::hasSkin` remain compatibility
mirrors; new consumers should use the owned vectors and `Node::skin`.
`AnimationSampler::outputComponents` describes one accessor element; channel
`components` describes one property value. Morph-weight values are flattened
SCALAR arrays with N weights per key, or per tangent/value group. Their keys can
be inspected now, but morph geometry/default weights and evaluation remain deferred.

CUBICSPLINE retains all in-tangent/value/out-tangent groups without normalizing
or sign-flipping their quaternions/tangents. Normalized integer rotation and morph
weight accessors are expanded using glTF rules. Non-animated TRS components retain
their authored/default values. `firstKeyTime` and `lastKeyTime` span all stored
samplers, including unused ones; they are diagnostics, not automatic playback
trims. Clip time still starts at zero even when the first key is later.

Channels without a node or with an unknown extension target are marked unusable
and diagnosed, not reassigned to a bone. Optional `KHR_animation_pointer` data is
not interpreted or advertised as supported; making that extension required still
rejects the file. Unsupported extension JSON is not retained by this stage.

Allocation accounting covers skin tables, matrices, clip/channel/sampler storage,
decoded keys, strings and temporary arrays. Validation includes finite data,
increasing nonnegative timestamps, required time bounds, output shapes/counts,
cubic key/tangent counts, duplicate node/property targets, zero rotation values,
joint references/order, influence attribute pairing/types and per-skin index bounds.
Every JOINTS_n/WEIGHTS_n set is retained; weights are not silently clamped,
renormalized or truncated. Full conformance (including weight-sum/duplicate-
influence and quaternion-unit tolerances) remains the Khronos validator's job.
Malformed newly decoded data can now fail explicitly rather than being ignored.

## Ownership and limits

`otglb::load(std::filesystem::path)` returns an owned `shared_ptr<const Asset>`
and diagnostics. Callers can reuse the asset after parsing has finished, without
retaining the input file or reopening it. Failure returns no partial asset.
No writing API is exposed. File caching, invalidation, image decoding and GPU
allocation are not part of this loader.

Default budgets are 1 GiB for file bytes, 256 MiB for parser allocations, 1 GiB
for decoded/work storage, and 1,024 hierarchy levels. JSON nesting is limited
to 256 levels before parsing. These are resource safeguards,
not polygon limits. Callers may supply `otglb::Limits` for a different budget.
The inspection utility uses the defaults.

## Build and test independently

A C++17 compiler and CMake are sufficient; Qt, OpenToonz libraries and OpenGL
are not required:

```text
cmake -S toonz/sources/glb -B glb-build -DCMAKE_BUILD_TYPE=Release
cmake --build glb-build --config Release
ctest --test-dir glb-build -C Release --output-on-failure
```

`BUILD_GLB_LOADER_TOOLS` defaults to ON for standalone builds and OFF inside
the full OpenToonz build. The static `otglb` target is built with OpenToonz;
the GLB Model FX links to it for read-only loading and CPU rendering. The dedicated
workflow runs on pull requests targeting any branch, so stacked PRs retain CI
coverage. The small `glbanimationdata_test` target uses this same opt-in gate;
no application-test defaults or workflows are changed.

The regression suite generates deterministic GLBs locally. It checks actual
geometry, transforms, material data, sparse/interleaved accessors, malformed
files, limits, source preservation and ownership. The animation-data suite adds
sampler layout, sharing, interpolation metadata, quaternion decoding, skin tables,
inverse binds, multiple influence sets, ignored extension targets and resource
boundaries. Linux CI also runs with AddressSanitizer and UndefinedBehaviorSanitizer.
Keep both valid complex models and intentionally invalid fixtures in acceptance.

The parser is [cgltf](https://github.com/jkuhlmann/cgltf), pinned as documented
in `thirdparty/cgltf/README.opentoonz.md`, with its MIT license retained. Use the
[Khronos Validator](https://github.com/KhronosGroup/glTF-Validator) to establish
fixture validity independently; loader checks do not replace full conformance
validation. Data layouts follow the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Bridge stack and merge checkpoint

This owned-data stage follows [bridge foundation #179](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/179).
The foundation's JSON capability manifest remains a historical snapshot pinned to
`88114ade`; this document describes the new loader, not that older baseline.
This change does not require PBR #177 and changes no renderer or FX controls.

**Merge status: NOT REQUIRED TO CONTINUE.** A Qt-independent pose evaluator can
be stacked on this data branch and tested through the existing standalone workflow.
The foundation must be integrated (or this branch deliberately rebased) before
this PR is integrated into main. This data layer must similarly land before a
dependent evaluator is integrated into main. Development and PR-head tests do not
require either merge. Recommend a merge checkpoint after the evaluator passes,
before adding playback UI/persistence as a third unmerged implementation layer.

## Initial loader verification (historical)

The initial loader passed 11 regression groups and also loaded these unmodified
Khronos GLBs from glTF-Sample-Assets commit `90d7ede14c7e280af263824604b427a1ca02cb66`:

| Asset | Triangles | Initial result |
| --- | ---: | --- |
| Box | 12 | Loaded |
| BoxInterleaved | 12 | Loaded |
| BoxVertexColors | 12 | Loaded |
| DamagedHelmet | 15,452 | Loaded, five embedded images retained |
| Fox | 576 | Loaded with warnings for three unevaluated animations and one skin |

Source SHA-256 hashes were unchanged after inspection. Sample files are not
redistributed; their descriptions and licenses are available in the
[Khronos sample repository](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/90d7ede14c7e280af263824604b427a1ca02cb66/Models).
These historical results are not a claim of playback support or a substitute for
running the current revision's regression suites and application acceptance.
