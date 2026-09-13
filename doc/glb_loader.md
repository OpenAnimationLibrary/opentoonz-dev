# Read-only GLB loader

This stage adds the `otglb` loading library and the `glb_inspect` command-line
utility. It follows the GLB Model FX framework. FX file selection does not call
the loader yet, and the FX still produces an empty image. Rendering and FX
connection integration are a later stage.

## Try a model

Download the `glb-inspect-<platform>` artifact from the **GLB Loader** workflow.
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
allocation peak.
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
  bytes. Images are not decoded and no material is shaded yet.
- `KHR_materials_unlit`, `KHR_texture_transform` and `KHR_mesh_quantization`.

Complex models are not rejected for having many parts or polygons. Recognition
of an animation, skin or morph target is reported, but deformation and playback
are not evaluated. Reported bounds describe base geometry, not an animated pose.
Unsupported optional extensions produce warnings when core data is usable;
unsupported required extensions (including compression codecs) fail explicitly.
Successful loading does not claim that a future renderer supports every feature.

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
no FX calls are connected to it at this stage. The dedicated workflow runs
on pull requests targeting any branch, so stacked PRs retain CI coverage.

The regression suite generates deterministic GLBs locally. It checks actual
geometry, transforms, material data, sparse/interleaved accessors, malformed
files, limits, source preservation and ownership. Linux CI also runs with
AddressSanitizer and UndefinedBehaviorSanitizer. Keep both valid complex models
and intentionally invalid fixtures in the acceptance process.

The parser is [cgltf](https://github.com/jkuhlmann/cgltf), pinned as documented
in `thirdparty/cgltf/README.opentoonz.md`, with its MIT license retained. Use the
[Khronos Validator](https://github.com/KhronosGroup/glTF-Validator) to establish
fixture validity independently; the loader's checks are not a replacement for
full glTF conformance validation.

## Initial local verification

The Linux regression executable passes 11 groups in release and with address /
undefined-behavior sanitizers. LeakSanitizer cannot run in the local container;
it is not disabled in the dedicated Linux CI job.

The inspection utility also loaded these unmodified Khronos GLBs from
glTF-Sample-Assets commit `90d7ede14c7e280af263824604b427a1ca02cb66`:

| Asset | Triangles | Result |
| --- | ---: | --- |
| Box | 12 | Loaded |
| BoxInterleaved | 12 | Loaded |
| BoxVertexColors | 12 | Loaded |
| DamagedHelmet | 15,452 | Loaded, five embedded images retained |
| Fox | 576 | Loaded with warnings for three unevaluated animations and one skin |

Source SHA-256 hashes were unchanged after inspection. Sample files are not
redistributed in this PR; their descriptions and licenses are available in the
[Khronos sample repository](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/90d7ede14c7e280af263824604b427a1ca02cb66/Models).
Windows and macOS execution is covered by the dedicated CI matrix, separately
from this local verification.
