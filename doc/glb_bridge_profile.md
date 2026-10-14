# A:M / OpenToonz GLB bridge foundation

**Profile 0.1 draft. Direction: Animation:Master (A:M) to OpenToonz (OT).**
This contract separates the target from implemented capabilities. It does not
claim complete glTF support or change GLB rendering, the FX UI, or A:M's exporter.
The exporter source is not part of this repository.

## Verified baseline and immediate boundary

The companion [capability manifest](glb_bridge_capabilities.json) pins the OT
baseline to `88114adeef1112773ab299acff98bcd4886d2f27`, after material colors and the
3D menu category. It deliberately excludes unmerged [PBR PR #177](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/177).
This is a versioned development snapshot, **not live feature negotiation**.
Update the manifest and this document together when an implementation lands.

The [owned Asset structures](../toonz/sources/glb/glbloader.h) retain geometry,
materials and image bytes, but only **counts/presence** for animations and skins.
Joint attributes can be retained without the skin's joint table, inverse bind
matrices or animation samplers. Morph target deltas, cameras, lights and arbitrary
metadata are not retained. Parser recognition is not ownership, and ownership is
not rendering support. See the [loader](glb_loader.md) and [FX](glb_model_fx.md).

Consequently, selecting animation keys requires an owned-data stage before a
runtime evaluator. Existing OT material keyframes are scene-owned overrides, not
playback of GLB-embedded material animation.

## Compatibility matrix

The wire representations below refer to [glTF 2.0][gltf] unless an extension is
named. Target behavior is a proposal; it must not be advertised as implemented.

| A:M data / bridge feature | Export contract | OT baseline | Target / acceptance |
| --- | --- | --- | --- |
| Patches / surfaces | Tessellate to indexed TRIANGLES; retain boundaries and explicit material assignments. | Triangle lists, strips and fans render. | No attempt to infer original splines from triangles. |
| Object hierarchy / repeated models | Named nodes, stable local transforms, shared meshes for instances. | Static hierarchy and instances render. | Keep source transforms; fitting is an explicit view operation. |
| Group / surface colors | Resolve effective surface per face before partitioning primitives. | Base RGB renders in Material Colors mode; legacy default is grayscale. | Black, inherited/default colors and independent equal-colored groups all survive. |
| Normals / tangents | Export smooth normals; generate tangents when maps require them. | Attributes retained; shading uses face normals. | Correct inverse-transpose normal transform and tangent handedness. |
| Decals / textures / UV sets | Bake unsupported layered/procedural appearances to suitable UV textures; embed PNG/JPEG. | References, samplers and encoded images retained; not sampled. | Respect UV selection, wrapping, filtering and KHR_texture_transform. |
| Vertex colors | COLOR_0 only where source data or deliberate baking provides it. | Retained; not rendered. | Multiply with material factor and base texture, not replace either. |
| Alpha / sidedness | alphaMode, alphaCutoff and doubleSided reflect intent. | Alpha ignored; all surfaces rendered two-sided. | OPAQUE/MASK/BLEND and culling; correct premultiplied OT raster output. |
| PBR / emission | Core factors/maps; calibrated approximation of A:M surfaces. | Factors retained; no physical response in pinned main. | #177 is a separate factor-based start; texture/alpha/environment support remain separate. |
| Bones / skin weights | Joint hierarchy, inverse bind matrices, JOINTS_n/WEIGHTS_n; normalize influences without silent truncation. | Attributes plus presence/count only; no skin evaluation. | Own complete skin data, then evaluate deformed geometry/normals. |
| Actions / object animation | Named clips; local node TRS, seconds, quaternion rotations. | Animation count only; static transforms rendered. | Deterministic random-access sampling, not frame-history playback. |
| Key interpolation | Preserve STEP/LINEAR/CUBICSPLINE when representable; otherwise controlled baking. | Not evaluated. | Quaternion slerp for LINEAR, proper cubic tangents and quaternion normalization. |
| Poses / SmartSkin | Stable-topology morph targets when feasible; bake evaluated results, not unsupported rig logic. | Presence only. | Retain deltas and weights; morph before skinning. |
| Animated material / camera / light properties | KHR_animation_pointer for permitted mutable properties. | Not supported; required extension rejects the file. | Explicit property allowlist; never arbitrary writes into the OT scene. |
| Cameras / choreography context | Core camera plus node transform; lens converted to vertical FOV using sensor context. | Camera objects discarded. | Optional imported camera, explicit aspect/crop handling; OT camera remains available. |
| Lights | KHR_lights_punctual for appropriate point/spot/directional equivalents. | Discarded. | Calibrated units; A:M area lights/glow/shadows are not equivalent by name alone. |
| Identity / provenance | Names plus optional versioned extras with persistent asset/entity IDs. | Arbitrary extras discarded; overrides use file hash and material index. | Explicit reconciliation on re-export, never silently transfer overrides by name/order. |
| Compression / variants / extra material extensions | Optional capability-gated additions, not a requirement for the initial bridge. | Unsupported required extensions fail; optional fallback may be usable. | Add each only with decoder/evaluator, fallback and fixtures. |
| Animation-only / skeleton-only GLB | Allowed as a separate interchange mode; geometry is not a glTF requirement. | No surface image without triangles; animation not evaluated. | Explicit target-skeleton mapping, not automatic name matching. |
| OT scene back to A:M | Later: image sequence plus timing/camera manifest. | Not implemented. | Choreography camera rotoscope/image planes first; do not force animated imagery into core static textures. |

## Export profiles and appearance contract

Provide separate **OT static baseline**, **rich interchange**, and later
**OT animated** presets. Never silently strip useful data from the rich profile
because the present renderer cannot display it. Conversely, an OT preset must not
promise that unsupported skinning/textures/animation will be used.

Geometry, materials/colors, textures, skeleton/skinning, each kind of animation,
cameras, lights and native round-trip payload must be deliberate choices. Geometry
is mandatory only for a profile promising a rendered surface. Bone animation
requires its target hierarchy; rigid-node animation does not require a skin.
A rig without animation is valid. Texture export requires corresponding material
bindings in these presets. Compression and native source payload default OFF.

Static export must explicitly choose **rest/bind geometry** or **baked selected
pose**. Simply removing a skin from a posed asset is not a reliable pose bake.
Bake transformations/deformations first when that option is requested. Do not
label unevaluated base geometry as the first frame of an Action.

Preserve the effective A:M color after inheritance and group/surface overrides.
Do not merge equal-colored materials when users need to recolor the corresponding
groups independently in OT. Names are labels, not unique identity. When textures
are disabled, use the actual underlying surface color, not an invented gray.
Unsupported layered decals and procedural materials require an explicit bake or
warning. Export reports must state which features were baked, approximated or
omitted. This OT PR does not claim those A:M exporter changes have been made.

The color calculation is material factor times base-color texture times optional
vertex color, with alpha handled according to alphaMode. glTF base-color factors
and vertex colors are linear; base-color/emissive texture RGB is sRGB, while
normal/occlusion/metallic/roughness maps are data. Confirm the A:M SDK's actual
color encoding using swatches before converting; do not double-linearize it.
Use alpha correctly at the GLB-to-premultiplied-OT-raster boundary. A specular
highlight must not overwrite a group's base color. A:M glow is not automatically
an emissive material, and diffuse/specular parameters are not an exact PBR match.

Use glTF's right-handed, Y-up coordinates and meters. Convert the chosen A:M
source units consistently for positions, translations, bind matrices, cameras and
light ranges. Never infer units from a model's apparent height. Existing exports
may contain legacy unit conventions; any override must be explicit, and applied
once. Scale factors are dimensionless. Do not silently recenter or auto-rescale.

## Animation data and evaluation boundary

The next data PR should own local TRS, skin joint indices and inverse bind matrices,
clip names, sampler inputs/outputs/interpolation, and channel target indices.
No references into freed cgltf memory may escape the loader. Apply the existing
allocation budgets and validate counts, finite values, indices and timestamps.
Unsupported content must remain diagnosed; retaining bytes is not a playback pass.

The evaluator should be a small Qt-independent layer over an immutable Asset,
returning a per-time pose. Cache immutable geometry separately from pose/render
state. Include asset revision, clip selection, time and overrides in applicable
cache keys. Allow independent clones and out-of-order/subframe/tile requests.
Never advance a mutable animation clock as a side effect of rendering a tile.

For a zero-based render frame f, requested scene start f0, scene rate F, speed v
and source offset o, use a documented mapping such as:

`sourceSeconds = o + (f - f0) / F * v`

Translate UI frame numbering at the boundary. Source FPS metadata is advisory;
GLB sampler timestamps in seconds govern playback. Support fractional time,
negative speed, freeze/manual time, trim, clamp and loop with explicit endpoint
rules; guard empty/zero-duration clips. A changed OT FPS must not reinterpret
source timestamps as frame numbers. Retain every source key, including the last.

Evaluate channels at that time, then the node hierarchy, morphs and skinning as
applicable. STEP holds the preceding sample; LINEAR rotations use quaternion
interpolation, not Euler interpolation. CUBICSPLINE stores in-tangent/value/
out-tangent per key; tangents are scaled by the interval and rotation results
normalized. Keyframe selection alone is not sufficient between key times.

Initial playback controls: clip, embedded-animation enable, source time or scene
time, offset, speed, trim and loop/clamp. Do not copy thousands of keys into the
Function Editor merely to play a clip. Promoting a channel creates an explicit
scene-owned override; define replace versus additive semantics and reset behavior.
Do not expose independently editable quaternion components as though they were
ordinary unrelated scalar rotations. No native channel editing is promised here.

[KHR_animation_pointer][pointer] is a later, allowlisted extension stage. Its
mutable-property rules are not permission to animate arbitrary JSON fields,
structural indices, array lengths or OT properties. Keep an appropriate static
fallback and honestly declare extensionsUsed/extensionsRequired. Do not rename a
private A:M payload with an unregistered KHR_/EXT_ name.

## Optional identity metadata (proposed, not consumed yet)

Use `asset.extras.amOtBridge` for `schemaVersion`, persistent `assetId`, optional
`sourceFps`, `sourceUnits` and `metersPerSourceUnit`. Use
`extras.amOtBridge.entityId` on nodes/materials/animations when needed. These are
private advisory extras, not a registered extension or a prerequisite for core
rendering. Do not include absolute workstation paths, credentials or native
source files by default. Persist IDs at authoring time; do not regenerate them
from array order or mutable names on every export.

Metadata does not change standard glTF units or promise exact .mdl/.act recovery.
Current hash-plus-material-index override identity remains unchanged. A later
reconciliation UI must check source lineage and handle missing/duplicate IDs and
conflicts before applying old overrides to a re-exported file.

## Read-only audit (available in this PR)

The [Python helper](tools/glb_bridge_audit.py) inventories GLB JSON against the
pinned [manifest](glb_bridge_capabilities.json). Python 3.9+ and the standard library
are sufficient; Qt, A:M and an OT build are not required.

```sh
python doc/tools/glb_bridge_audit.py model.glb
python -m unittest discover -s doc/tools/tests -p 'test_glb_bridge_audit.py' -v
```

It reports all stored objects, material/primitive associations, attributes, clip
channel/interpolation counts, known loader blockers, and separate retention and
rendering status. It does not follow resource URIs or modify the input. It caps
file size at 1 GiB, JSON at 16 MiB, nesting at 128 and chunks at 64. These are
inventory-tool safeguards, not new restrictions in the production OT loader.

**Not a validator or renderer.** It skips binary payloads, does not decode keys,
weights or images, does not establish active-scene visibility, and cannot prove
that colors match the original A:M model. No detected blocker is NOT a
compatibility pass. Use the production `glb_inspect`, [Khronos validator][validator]
and actual rendered comparisons for acceptance. Exit 0 means inventory completed,
3 means a known baseline loader blocker was found, 1 means inventory failed, and
2 is command-line misuse. Redirect stdout to save a JSON report.

This helper and its small synthetic tests live under documentation tooling, are
not linked into OT, and do not duplicate the production cgltf decoder. They are
manual/opt-in, with no workflow or default-build changes. Synthetic inventory
fixtures are not independently validated complete glTF assets. Do not commit
user-supplied character models or unlicensed third-party fixtures.

## Implementation sequence and merge gates

Use real code dependencies, not an assumption that every PR must stack.

| Stage | Base / dependency | Validation before integration | Merge guidance |
| --- | --- | --- | --- |
| This profile + audit | main; independent of #177 | Python inventory tests and private local sample audits | **NOT REQUIRED TO CONTINUE.** No runtime dependency or application build. |
| Owned skin/animation data | main or the unmerged foundation branch | Standalone loader ownership, malformed data, limits; sanitizers | Continue virtually on an unmerged branch; request merge once its data API is stable. |
| Appearance: normals, colors, maps, alpha | Coordinate with #177 where renderer code overlaps | Swatch/UV/alpha fixtures and tiled 8/16/float comparisons | #177 need not merge for development; stack only changes that actually use it. |
| Pose evaluator + skinning | Owned-data branch | Known poses, interpolation, bind/skin math, clone/concurrent/random-order requests | Parent required in main **before child integration**, not before virtual development. |
| FX playback UI and persistence | Evaluator branch | Standalone tests plus targeted Windows/Qt/package test and manual A:M-to-OT clip check | Recommend merging the proven lower layers before extending beyond two dependent unmerged PRs. |
| Overrides, morphs, pointer animation, cameras/lights | Relevant accepted lower layer, independently scoped | Feature-specific fixtures and saved-scene compatibility | One capability at a time; do not advertise future support in the baseline manifest. |

Every implementation PR should state **Base/depends on**, **Merge status**, the
next permitted unmerged step, and the evidence needed before requesting merge.
Use NOT REQUIRED TO CONTINUE, RECOMMENDED CHECKPOINT, or REQUIRED FOR MAIN
INTEGRATION with a concrete reason. A build artifact is not inherently a reason
to merge: test/package the actual PR head with a supported PR/manual workflow.
After a parent merges, retarget/rebase its children and check that the child diff
contains only its own change. Do not blindly retarget a stack after squash merge.

Keep at most two dependent unmerged implementation PRs as the default working
limit; make an explicit exception only when it saves work. Prefer small virtual
standalone tests during iterations, then one consolidated application gate for
an integration candidate. Preserve existing off-by-default GLB application tests.
Do not disable safety checks or call a virtual standalone pass a Windows UI pass.
Do not merge automatically; the maintainer chooses when validated changes land.

## Acceptance assets for the subsequent stages

Use tiny reproducible fixtures before complex characters: inherited black/red/
green/blue colors; two equal-colored but independently editable groups; embedded
PNG/JPEG with nonwhite tint; UV seams and texture transforms; MASK/BLEND edges;
mirrored/nonuniform transforms; two-joint skin with known inverse binds; STEP,
LINEAR and CUBICSPLINE including quaternion sign handling; two named clips and
mismatched source/scene FPS; required versus optional extensions and absent data.
Include clips that omit a node component, hierarchy-only animation and unused
objects. Add a complex privately supplied A:M export as a supplemental regression,
not as proof that every exporter feature is correct.

For each feature record exporter version/options, OT commit, validator outcome,
loader inventory, source hash before/after and the actual render comparison.
A future OT-to-A:M route should first package imagery/timing/camera references for
choreography rotoscopes, without delaying the forward GLB bridge.

[gltf]: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
[pointer]: https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_animation_pointer/README.md
[validator]: https://github.com/KhronosGroup/glTF-Validator
