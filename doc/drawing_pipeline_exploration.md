# Drawing pipeline exploration (Exporatory)

## Scope, history and attribution

This note accompanies the revival of [OT-Dev PR #127][otdev-pr] following a
review of [OpenToonz PR #7068][upstream-pr]. The goal is to improve drawing
accuracy and responsiveness across input, reconstruction and presentation,
while making each stage observable. This is an exploratory OT-Dev update;
individual fixes should be evaluated separately before proposing them upstream.

The history contract is to retain the original diagnostic head
`b88c8d2cf35169a6367eea74b97b4977443e9d62`, merge OT-Dev main at
`fd556ca6467bb84149e62db8ecb19fcfab43070e`, then add focused implementation
commits. Existing OT-Dev behavior and additions are the integration baseline.

The preceding-segment tangent correction is credited to
**konero <konero@users.noreply.github.com>**, from
[`485a3e7a39deaca420bb0eed8f93cadba8853a15`][tangent-source],
`fix(brush): smooth fast tablet strokes`, associated with PR #7068.
The broader timing, simplification, presentation and diagnostics changes in
this exploration should not be attributed to that small correction alone.

This work was AI-assisted by **ChatGPT, GPT-6 Astra Pro**. The user requested a
deep review of the core drawing problem, then authorized the whole update path
in OT-Dev with minimal initial testing. This document records implementation
and validation status; it does not claim a completed application build or
independent human review. See the repository's [development checklist][dev]
and [AI-assisted development checklist][ai].

## 1. Input boundary: preserve meaningful samples

`SceneViewer::tabletEvent()` now uses the same move-forwarding rule on all
platforms. A stationary sample can still change pressure or elapsed time.
The viewer only suppresses a repeated move with a positive, matching source
timestamp and matching position, pressure, buttons, modifiers, device and
pointer type. Unknown timestamps are not deduplicated by position alone.

Press and release handling keeps the existing lifecycle, including duplicate
press handling and the zero-pressure move fallback that finishes an active
stroke. Accepted new presses reset the stroke sample clock. Move deduplication
does not gate presses or releases.

The existing 20 ms busy timer supplies the high-frequency marker to tools on
every platform. Raster Selection keeps its deformation guard; drawing tools
can consume the intervening samples. This avoids making the viewer's timer a
global drawing-rate limit. The Windows Ink history handler also returns its
accumulated handled result instead of losing it to a shadowed local variable.

This change does not switch tablet APIs, change Qt compression attributes,
introduce Qt 6, or recover samples never delivered by the platform.

## 2. Separate sampling time from delivery order

The application previously used one timeline for physical stroke intervals
and input-state ordering. Those purposes diverge when events arrive in batches.

| Value | Meaning and units | Used for |
| --- | --- | --- |
| `TMouseEvent::sourceTimestamp()` | Original Qt timestamp, milliseconds | Source interval calculation and diagnostics |
| `TMouseEvent::time()` | Application delivery ticks | Ordering key, button and track events |
| `TMouseEvent::sampleTime()` | Stroke-relative seconds; negative means unavailable | Handoff to the input manager |
| `TTrackPoint::time` | Sample elapsed seconds, adjusted by track offsets | Brush dynamics and sample interpolation |
| `TTrackPoint::eventTime` | Delivery seconds relative to the track origin and offset | Input history and chronological track merging |

`TStrokeSampleClock` derives elapsed intervals from the source clock without
forcing it into the application's dispatch epoch or clamping it to arrival
time. Thus 4 ms source intervals remain 4 ms when delivered 0.1 ms apart,
including the first batch of a stroke.

Its conservative fallback policy is explicit:

- Repeated source timestamps retain equal sample time and are marked estimated.
- A missing timestamp within a sourced stroke holds sample time until a valid
  timestamp supplies the interval. A delayed delivery does not move the source
  anchor forward.
- A backward source reset or wrap starts a new epoch at the current sample
  time; the unknown interval at that boundary is not reconstructed.
- A stroke with no source timestamps uses nonnegative delivery intervals and
  marks them estimated. A later first valid source establishes a new anchor.

Track sample time is nondecreasing. Delivery ordering can advance by one tick
to break ties; that ordering adjustment is never substituted for physical pen
motion. The existing constructor behavior remains available for legacy callers
that do not supply a separate `eventTime`.

`touchTracks()` uses the same relative delivery conversion as `trackEvent()`.
Key changes and synthetic finalization retain the previous sample time, since
they do not add a measured pen interval. This also fixes the former mix of
absolute uptime and stroke-relative seconds. History holders use `TTimerTicks`,
avoiding a Windows `long` truncation in the button-history interface.

Copies, track offsets and linear/spline interpolation propagate both clocks.
Key/button lookups use delivery time, and the chronological merge converts
tick differences with `TToolTimer::step`. Full Color, Toonz Raster and Toonz
Vector brush handlers all pass the event's delivery and sample times onward.
The MyPaint wrapper normalizes negative/nonfinite intervals to zero and keeps
zero-time repeats and the existing final interpolation flush valid; it does
not replace the sample timeline with fabricated nanosecond dynamics.

## 3. Reconstruction: stable prefixes and a bounded accuracy budget

Updating a tangent changes the preceding segment as well as the new segment.
The credited correction marks that preceding segment for reconstruction while
preserving the already fixed prefix. This addresses a geometry dependency;
correct timestamps alone do not repair stale vector tangents.

The simplifier now checks the deviation of incoming samples from a replacement
segment and checks pressure error. It retains actual incoming points, so a
retained position, pressure, tilt and both times belong to the same sample.
It does not construct a point by mixing one position with another pressure.
Corners, pressure extrema, endpoints and stationary spans receive protection.
A 64-point candidate limit bounds each replacement check and limits a stable
straight span between retained anchors to 63 source-index steps.

Only the provisional tail is reconsidered. An anchor becomes fixed after the
source point which selected it becomes fixed upstream. This is the contract
used by incremental consumers to avoid repainting or deleting committed input.

For the vector brush, Accuracy supplies an error scale
`E = 30 / (1 + 0.5 * Accuracy) * pixelSize`. Simplification receives
`min(2 * pixelSize, 0.25 * E)` and final fitting receives the remaining budget.
Pressure error is checked after the cubic response used by vector thickness,
with a tolerance constrained by that geometric scale and the thickness range.
These budgets are construction parameters, not a proof of final error against
the artist's intended continuous curve.

Smooth remains the separate, intentional smoothing control. The final vector
path no longer runs the old endpoint-thickness trimming heuristic, which could
delete deliberate start/end detail. The fitting scale is captured with the
stroke's modifier configuration, rather than recalculated from a later zoom.

## 4. Presentation: reduce repeated work without changing dab order

The vector preview caches strip-edge coordinates on the CPU in image space.
Changes invalidate the affected tail and neighbor; full redraws reuse stable
normals while still drawing the whole preview. The zero-width preference
invalidates the cache. The drawing path batches compatible strip and line
primitives and preserves primitive order when depth or stencil testing is on.
This is a CPU geometry cache, not a retained GPU framebuffer or a claim of
constant-cost redraw. Vertex submission and rasterization still scale with
the stroke, and filled previews retain their existing path.

For soft Toonz Raster drawing, `BluredBrush::addPoint()` reports whether spacing
actually emitted a dab. A suppressed dab no longer triggers the corresponding
tile-save, destination update and invalidation work. The final point still
bypasses the spacing suppression. Work/backup raster preparation remains in
its existing order.

Full Color and Toonz Raster accumulate invalidation rectangles within an
`inputPaintTracks()` pass and issue one presentation request at its end.
Rasterization, compositing and undo work still occur in input order. This does
not defer paint samples, add a frame scheduler, or merge independent strokes.

## 5. Observe and replay the stages separately

Opt-in diagnostics record raw and dispatched tablet events in viewer logical
coordinates, accepted input and reconstructed tracks in tool coordinates,
and vector fitting output. Session, stroke and track identifiers distinguish
streams. Sample and delivery times remain separate in the records.

Accepted-input replay enters at `TInputManager::trackEvent()` and can vary the
number of samples processed per batch while retaining recorded intervals.
It exercises the downstream modifier/brush path, not the native tablet API,
viewer forwarding, coordinate conversion, or a real-time event scheduler.
It does not reconstruct unrecorded key/button history or tool settings.

The analyzer groups streams before computing timing, pressure and geometry
statistics. Control polygons are distinguished from sampled fitted curves.
Circle fits describe observed samples; an explicit reference is required to
measure deviation from a known circle. Chord error and vertex radial error
are separate, and the sampled curve output is not an exact continuous-curve
Hausdorff measurement. Diagnostic logging itself can affect performance.
For commands, environment variables and capture/replay examples, use the
[drawing stroke diagnostics guide][diagnostics].

## 6. Evidence and remaining validation

Initial validation deliberately used small source-level probes with limited
dependency stubs, rather than a broad test matrix:

- The clock/track probe preserved 4 ms intervals in an initial rapid batch,
  covered repeated/missing/reset timestamps, and verified relative touch time,
  history lookup, interpolation, offsets and finalization at 100 s uptime.
- The joint tangent/segmentation/simplification probe produced matching circle
  output for one-at-a-time, seven-at-a-time and whole-stream processing,
  including both timelines. Fixed prefixes remained unchanged; focused cases
  retained corners, a pressure peak, endpoints and the bounded straight span.
- The preview probe matched before/after primitive coordinates for targeted
  add/pop, width, zero-width, copy and clear cases. It did not render a real
  OpenGL framebuffer or measure frame latency.
- Extracted raster methods verified dab-result/endpoint behavior and rectangle
  accumulation for both brush classes. In one 101-position synthetic sequence,
  existing spacing emitted 10 or 6 dabs for widths 100 or 200 respectively;
  these are operation counts, not measured application speedups.

A complete application build and interactive tablet validation remain pending.
Next checks should establish compilation, then inspect pressure-only changes,
stroke endpoints, Selection deformation and representative drawing with real
captures. End-to-end latency, long-stroke CPU/GPU cost and visual equivalence
need application measurements with diagnostics overhead accounted for. No
percentage speedup or hardware sampling-rate improvement is established here.

[otdev-pr]: https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/127
[upstream-pr]: https://github.com/opentoonz/opentoonz/pull/7068
[tangent-source]: https://github.com/opentoonz/opentoonz/commit/485a3e7a39deaca420bb0eed8f93cadba8853a15
[dev]: development_checklist.md
[ai]: ai_assisted_development_checklist.md
[diagnostics]: ../.github/DRAWING_STROKE_SMOOTHNESS_DIAGNOSTICS.md
