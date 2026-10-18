# Explore Bullet Physics

This separate OT-Dev experiment connects ordinary drawing columns to Bullet CPU
rigid bodies, previews their motion, and bakes it into editable scene keys.
It is based on `main`, independently of [the Skeleton Tool exploration
(#248)](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/248).

The earlier PyBullet timestep experiment provided background, but no existing
Bullet implementation was found in OT-Dev's current code, PRs, or matching
branch names. This implementation uses Bullet's native C++ libraries; Python
and a GPU are not runtime dependencies.

## Build

`WITH_BULLET` defaults to `OFF`. An ordinary build has no Bullet dependency or
physics command. For this experimental build, obtain the tested single-precision
Bullet 3.25 SDK:

```sh
git clone https://github.com/bulletphysics/bullet3.git bullet-sdk
git -C bullet-sdk checkout 2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5
```

Add these options to the usual OpenToonz CMake configuration, using an absolute
path to that checkout:

```text
-DWITH_BULLET=ON
-DBUILD_BULLET_TESTS=ON
-DOT_BULLET_SOURCE_DIR=/absolute/path/to/bullet-sdk
```

The SDK checkout is a build input, not a submodule. CMake does not download
anything for Bullet. It builds only LinearMath, BulletCollision and
BulletDynamics as static libraries, without importing Bullet's top-level
compiler flags, demos, Python, GPU or other optional packages.

Alternatively, omit `OT_BULLET_SOURCE_DIR` and provide a compatible installed
single-precision SDK discoverable by CMake's `FindBullet`, for example through
`BULLET_ROOT`. Packagers using shared system libraries must include the required
runtime libraries. The checked-in zlib notice is
`stuff/doc/LICENSE/LICENSE_bullet.txt`; retain it in distributions.

Windows PR application CI enables the exploration and runs its native bake tests
before packaging. The separate Bullet workflow tests the engine on Windows,
Linux and macOS, plus Linux address/undefined-behavior sanitizers.

## Try a pendulum

1. Expose a drawing for the desired frame range. Set its ordinary column pivot
   to the intended attachment point; a drawing extending to the right makes
   an easy pendulum example.
2. Select that column or its cells in the Xsheet, then open
   **Windows > Explore Bullet Physics...**. Existing room menus also receive
   the command in a Bullet-enabled build. It is available to shortcuts and
   custom menus as `MI_ExploreBulletPhysics`.
3. Leave **Motion = Dynamic**, choose **Connect to = World**, and choose
   **Joint = Hinge**. Set **Direction = -90 degrees** for a proxy extending
   to the right, and adjust its length to match the drawing.
4. Choose a frame range, then click **Simulate**. Scrub the dialog's slider to
   inspect its proxy preview. The scene is unchanged at this point.
5. Click **Bake X/Y/Rotation Keys** to apply the result. Playback and ordinary
   frame scrubbing now use native animation keys. One Undo restores the original
   three channels; Redo reapplies the cached result without another simulation.

For a chain, parent the drawing columns with the existing Skeleton Tool first,
then select the chain and open the dialog. Selected parent relationships supply
initial connections. A root with a selected child defaults to **Pinned**;
children default to dynamic bodies connected to their selected parent. The first
selected child supplies an initial proxy direction and length. Adjust leaf
proxies and any mismatches before simulation.

The table permits connections to any other selected column, independently of
the drawing hierarchy. **Free** creates no joint. **World** creates a joint to
a static world anchor. The column hierarchy itself is never reparented.

| Control | Meaning |
| --- | --- |
| Dynamic | Bullet determines the body's motion from its starting pose. Later authored X/Y/rotation motion is replaced when baked. Initial velocity is zero. |
| Pinned | Static body at the starting pose. Its authored world placement must remain stationary throughout the bake range. Its channels are not baked. |
| Hinge | Attachment allowing rotation about the drawing-plane normal, with angular limits relative to the starting connection. |
| Point | Shared attachment point with free planar rotation. |
| Fixed | Preserve the initial relative position and orientation. |
| Length / Direction | Rectangular proxy extending from the column pivot along its chosen world direction; zero degrees extends upward. |
| Proxy width / Dynamic mass | Common width and mass for this simulation; these are approximations of the artwork. |
| Meters per OT inch | Simulation scale, initially 1. This is deliberately explicit: one displayed OT inch represents one simulation meter by default. |
| Steps per frame | Fixed subdivisions of the scene's output frame rate. Five steps at 24 fps give 120 steps per second. |
| Floor | Static horizontal plane; its height is entered in OT inches. |

Each connection starts at the driven column's pivot. That shared world anchor
is converted to local coordinates on both bodies. Adjacent connected proxies do
not collide with each other; other proxies and the optional floor do collide.
Duplicate connections, self-connections, missing endpoints and connections
between two pinned endpoints are rejected. Physical loops involving distinct
connections are permitted; they do not add recursive stage-object dependencies.

## Evaluation and baking

- Simulation is explicitly requested and runs in a fresh, single-threaded world.
  The first output sample is exactly the starting pose. Each later sample
  advances one scene frame through a fixed number of explicit Bullet steps.
- Scrubbing the proxy preview reads the completed clip in arbitrary order;
  it never advances a live physics world from the viewer or renderer.
- The first pass supports 1–64 drawing bodies and 1–1200 frames. Positions and
  sizes have finite bounds; invalid input or divergent motion fails cleanly.
- Baking preflights every sample and creates a complete patch before writing
  channels. It converts world motion through each actual or simulated parent,
  respecting the original column center and offset. An affine result requiring
  scale, shear or pivot changes is rejected with a diagnostic.
- X/Y/rotation keys, including fractional keys, inside the chosen range are
  replaced by integer-frame Linear keys. Keys outside the range retain their
  data, although interpolation adjoining the baked interval can change.
  The full original channel snapshots are retained for Undo/Redo.
- Drawing exposures, parent links, Z, stacking, scale, shear and source artwork
  are retained. Existing unselected children inherit the baked parent motion
  through ordinary parenting.
- Successful results save as ordinary scene keys, with no new scene tags and
  no Bullet dependency when reopening or rendering. Connection settings and
  the preview clip are temporary dialog state, not saved scene data. Reopening
  the dialog reloads the selection and recreates the initial connection table.
- Replay is tested within the same binary. Identical results across Bullet
  releases, compiler settings and platforms are not promised. Baked keys are
  the portable result.

## Current boundaries

This pass uses rectangular proxies and planar translation/rotation; it does not
extract artwork collision contours or simulate 3D stage placement. Paths,
legacy pinned IK, stage-object cycling and drawing-hook pivots are excluded.
Animated pinned bodies need a future kinematic mode. Use constant body scale
and rigid or uniformly scaled parents for the first bake tests.

There is no automatic simulation during playback, persistent physics graph,
spring/motor control, rest-pose blending, force field, cloth, Plastic deformation,
or named vector CP binding in this pass. The proxy preview does not display
artwork. Viewport interaction and rendered results still require application
testing on representative production rigs.

## Connections to the bones exploration

`physics/bulletworld.h` isolates engine inputs and outputs from OpenToonz and
Bullet types. A `Body` supplies a world pivot, direction, dimensions and mass;
a `Connection` supplies two body endpoints, an anchor and joint settings.
`bulletstage.cpp` is the first adapter, from columns to those physics inputs
and from simulated world poses back to stage channels.

For #248 integration, resolve persistent rig UUIDs to a temporary body-index
map, use persistent bone tips for proxy direction/length, and expose joint
anchors at Pivot/Bone Tip. This keeps transient Bullet indices out of scene
identity. A later shared endpoint model can add one-way Plastic vertex and
named vector-group bindings, with explicit ownership and dependency checks.

Keep Aim At, Orient Like, ordinary parenting and IK in the artist rig system.
Add physics as a separately controlled source of motion, with a defined order
relative to those constraints. Next experiments should compare kinematic
controllers, spring joints, enforcement blending, persistent connections and
cache invalidation before attempting live playback integration.

## Verification

The actual Bullet connection kernel builds without Qt or OpenToonz:

```sh
cmake -S toonz/sources/physics -B bullet-build -DCMAKE_BUILD_TYPE=Release -DOT_BULLET_SOURCE_DIR=/absolute/path/to/bullet-sdk
cmake --build bullet-build --config Release --parallel 2
ctest --test-dir bullet-build -C Release --output-on-failure
```

In a Bullet-enabled application configuration, build `bullet_world_test` and
`bullet_stage_test`, then run:

```sh
ctest --test-dir toonz/build -C RelWithDebInfo --output-on-failure -R '^bullet_(world|stage)$'
```

Kernel tests exercise free fall at two frame rates, frame-zero semantics, world
and body joints, hinge limits, fixed links, a two-link chain, pinned roots,
planar motion, a floor, repeatable replay and invalid input. Native stage tests
exercise parented world/local conversion, scaled/rotated parents, nonzero
centers/offsets, arbitrary-order scrubbing, fractional keys, outside keys,
repeated snapshot Undo/Redo, rejection before mutation and an actual Bullet
free-fall bake.

Manual checks: single-column pendulum; three-column chain; free prop and floor;
all three joint types; rotated/translated/scaled parents; locked columns;
invalid/duplicate links; scene save/reopen; playback and render after baking;
Undo/Redo; existing room menus; and a default build with Bullet disabled.

Requested by Rodney Baker. Implementation and documentation prepared with
AI assistance from OpenAI Codex. Bullet source remains unmodified; no
Animation:Master source code or assets are included.
