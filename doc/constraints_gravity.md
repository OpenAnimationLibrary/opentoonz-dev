# Fixed, Mobile and gravity baking

This extends **Add Constraints** (#212) with a small, gravity-only workflow.
The separate Bullet exploration (#249) is unchanged and remains available for
later investigation. This workflow has no Bullet dependency or build switch.

## Use it

1. Expose drawings across the intended frame range. In the **Stage Schematic**,
   right-click a drawing column or pegbar and open
   **Constraints**. Choose **Mobile** for each object that should fall.
2. Leave other objects **Fixed**, which is also the automatic default when no
   motion assignment exists. Fixed objects are excluded from gravity and retain
   their authored world motion. To keep an object stationary, leave its ordinary
   animation channels stationary too.
3. Optionally add **Aim At…** to an object that should turn toward a target.
   Aim At can coexist with either Fixed or Mobile.
4. Choose **Constraints > Simulate and Bake…** from any drawing-column or pegbar
   menu. Set **First frame**, **Last frame** and **Gravity**, then click
   **Simulate and Bake**. The process uses all Mobile assignments in the current
   Xsheet; selection does not limit the simulation.
5. Play or scrub the scene normally, save it, or use one **Undo** to restore
   every changed animation channel. Redo restores the prepared keys without
   running another simulation. Assignment edits and removals also support Undo.

The first frame retains the Mobile object's starting position. Each Mobile
object begins with zero velocity and accelerates downward. Gravity is expressed
in OpenToonz inches per second squared; the default is an adjustable artistic
strength of 9.81. The scene's output frame rate determines elapsed time.
Later authored position keys on Mobile objects are replaced within the chosen
range. Authored rotation, scale and shear remain animation inputs.

Fixed and Mobile are mutually exclusive assignments on an individual object.
There is no automatic Mobile inheritance: mark children Mobile explicitly when
they should fall too. An unassigned or Fixed child below a Mobile parent receives
the local compensation needed to retain its authored world motion. A Mobile
child below a Mobile parent receives gravity once, rather than falling twice.
Unrelated Fixed objects keep their original keys.

Aim At stays enabled after the bake and follows the resulting keyed positions.
Its authored base rotation and influence are not flattened and reapplied.
If a fixed descendant needs compensation while its parent aims at a falling
target, that compensation is included in the same Undo operation.

## Scope and saved data

- The initial simulation applies downward gravity only. There are no body
  proxies, collisions, floor, joints, springs, mass controls, initial velocity
  or direction controls. Repel/repulse behaviors and their initial speed and
  direction settings are follow-up work.
- Existing Buffer data from the earlier experiment remains readable, visible in
  **Show Constraints**, and removable through the existing removal commands.
  New Buffer setup is hidden. Remove active Buffer assignments before using the
  gravity bake; they are not interpreted as physical forces.
- Fixed/Mobile assignments save with the existing constraint records. Baked
  motion uses ordinary position keys and any rotation keys required for parent
  compensation. No preview cache or running simulation is needed for playback
  or rendering. Aim At continues to use this PR's existing live evaluator.
- The bake prepares the entire patch before changing channels. Only affected
  X/Y/rotation channels are written. Keys inside the range, including fractional
  keys, are replaced by integer-frame Linear keys. Outside key data is retained,
  although adjacent interpolation can change. Undo retains complete original
  channel snapshots.
- The first pass supports 1–1200 frames and up to 512 stage objects. Affected
  objects must be unlocked and use XY motion without paths, legacy pinned IK,
  cycling or drawing-hook pivots. Dependency cycles, non-finite transforms and
  results requiring scale/shear changes are rejected before any channel changes.
  Use rigid or uniformly scaled parents for these first tests.

## Verification

Configure the usual OpenToonz build with `-DBUILD_CONSTRAINT_TESTS=ON`, then run:

```sh
cmake --build toonz/build --config RelWithDebInfo --target constraintsimulation_test
ctest --test-dir toonz/build -C RelWithDebInfo --output-on-failure -R '^constraint_gravity$'
```

Native tests cover default assignments, replacement and snapshot restoration,
cloning, repeated stage save/reopen, frame-zero semantics, frame-rate-independent
free fall, nonzero centers, scaled/rotated parents, fixed descendants, nested
Mobile objects, live partial-influence Aim At, compensation beneath an aiming
parent, fractional/outside keys, repeated bake Undo/Redo and rejection before
mutation. The Windows application workflow builds and runs this test before
building and packaging OpenToonz.

Manual review: expose a drawing for the frame range; mark it Mobile; bake and
scrub; Undo/Redo; repeat with a Fixed sibling, an unassigned child, two nested
Mobile objects and a fixed Aim At tracker; save/reopen; check ordinary playback
and rendering. These application interaction checks remain part of review.

Requested by Rodney Baker. Implementation and tests prepared with assistance
from OpenAI Codex.
