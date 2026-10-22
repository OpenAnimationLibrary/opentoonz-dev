# Drawing accuracy and performance diagnostics

This is the diagnostic companion to the revived OT-Dev [PR #127](https://github.com/OpenAnimationLibrary/opentoonz-dev/pull/127), **Improve drawing accuracy and performance (Exporatory)**. The implementation and current evaluation limits are described in [the drawing pipeline note](../doc/drawing_pipeline_exploration.md).

The recorder separates input delivery, accepted tool samples, modifier output, and fitted vector geometry. Comparing these stages helps locate a change without assuming that every visible corner is caused by a tablet driver or by smoothing.

## Enable recording

Set these environment variables before launching OpenToonz:

```text
OPENTOONZ_INPUT_TRACE=1
OPENTOONZ_INPUT_TRACE_DIR=<optional writable directory>
```

For example, in PowerShell:

```powershell
$env:OPENTOONZ_INPUT_TRACE = "1"
$env:OPENTOONZ_INPUT_TRACE_DIR = "C:\stroke-traces"
```

Without a directory override, files are written under `opentoonz-stroke-diagnostics` in the system temporary directory. A session name contains UTC date/time and process ID. The recorder is disabled by default and has no tool-options UI.

The viewer executable and tools call exported functions in `tnztools`; one out-of-line state owns the session, IDs, and buffers. Each output buffers up to approximately 256 KiB and flushes at stroke completion or when full. This avoids a filesystem flush for every sample. A trace write/flush failure is reported through Qt's warning output. An abnormal process exit can lose the most recent buffered rows.

Recording still adds measurement overhead. Turn it off for drawing throughput or latency measurements.

## Files and coordinate spaces

| File | Stage | Contents and interpretation |
| --- | --- | --- |
| `stroke-<session>-raw.csv` | `raw` | Tablet events observed at the viewer, with original source timestamp, arrival timestamp, pressure, device/pointer identity, buttons, modifiers, and the high-frequency marker. Coordinates are Qt logical viewer coordinates with a top-left origin. |
| Same raw file | `dispatched` | Events actually sent to the viewer's press/move/release handlers, including mapped sample time and its estimated flag. This is a viewer-dispatch observation; it does not assert that the drawing tool accepted the event. Coordinates are also logical viewer coordinates. |
| `stroke-<session>-accepted.csv` | `accepted` | Samples accepted into input-manager tracks, in tool coordinates, including pressure, tilt, device/touch identifiers, flags, and both time domains. `event_type` distinguishes physical samples, synthetic state touches, and synthetic finish points. |
| `stroke-<session>-geometry.csv` | `reconstructed` | Completed output tracks after the modifier chain. These are reconstructed track points, before vector fitting. |
| Same geometry file | `fitted_control` | The vector stroke's quadratic control points after fitting and the current auto-close adjustment. These are control points, not a polyline drawn by the brush. |
| Same geometry file | `fitted_curve` | Each fitted quadratic evaluated at 32 subdivisions, including stroke endpoints. The `thickness` field is the stored vector radius. These samples approximate the curve for diagnostics. |

Every accepted/geometry record carries a session, stroke, root-track, and output-track identity. Raw events outside an active tablet gesture use stroke `0`; do not interpret the combined hover group as a drawn stroke. Mouse-only accepted tracks receive their own stroke IDs.

Raw and dispatched modifier fields reflect their respective Qt and `TMouseEvent` masks; their bit values are not interchangeable. Likewise, raw tablet device identity and the brush's logical input device ID serve different purposes.

The recorder does not currently capture raster pixels, undo state, brush presets, viewer transforms, key/button-history transitions as replay commands, or frame-range interpolation results. Record the revision, OS/input backend, tool, level type, brush preset, brush size, Accuracy, Smooth, zoom, and camera DPI alongside a comparison. Raw viewer coordinates must never be replayed as tool coordinates.

## The two clocks

| Field | Unit and meaning |
| --- | --- |
| `source_timestamp` | Original Qt/device timestamp in milliseconds. Zero means unavailable. |
| `arrival_timestamp` | Application delivery ticks, in nanoseconds. |
| `pipeline_timestamp` | Input-manager ordering ticks, in nanoseconds. |
| `sample_time` | Stroke-relative physical sample elapsed seconds, used by brush dynamics. |
| `event_time` | Relative delivery/ordering seconds, used by key/button history and chronological track merging. |
| `time_estimated` | Viewer timing is fallback, repeated, missing, or reset rather than a newly advancing source-clock observation. |

Valid source intervals survive delayed delivery batches. Repeated or missing timestamps inside an established source-clock stroke hold the physical sample clock until a valid source interval is available. This intentionally does not invent elapsed motion time. A stroke without source timestamps falls back to elapsed delivery time. Source-clock reset/wrap begins a new epoch at the current sample time.

Synthetic key-state touches and finalization keep the last sample time and advance only event ordering. The MyPaint wrapper preserves zero durations and replaces negative/nonfinite durations with zero; the existing interpolation flush also uses zero. Missing hardware timing cannot be reconstructed by this diagnostic path.

## Replay

Set an accepted/tool-coordinate CSV as the source before launch:

```text
OPENTOONZ_INPUT_REPLAY=C:\stroke-traces\stroke-accepted.csv
OPENTOONZ_INPUT_REPLAY_STROKE=1
OPENTOONZ_INPUT_REPLAY_BATCH=1
```

`OPENTOONZ_INPUT_REPLAY_STROKE` is optional. Without it, replay selects the first session/stroke in the file. Selection prevents unrelated strokes from being joined. Legacy accepted CSVs without the new time/identity columns remain supported; their elapsed sample time is estimated from pipeline timestamps.

Select the intended drawing tool and level, then begin one gesture. The first track event is replaced with the selected replay stream. The remainder of that physical trigger gesture is suppressed until its final event. Replay runs once per process launch. A recording without a final point is explicitly finished at its last sample for each device/touch pair.

Each replay sample goes through `TInputManager::trackEvent()`. By default `processTracks()` runs after every sample, preserving incremental reconstruction. `OPENTOONZ_INPUT_REPLAY_BATCH` can group already available samples before processing, from 1 through 65536; a final sample always flushes the group. For example, compare batches of 1 and 7 with the same input stream to detect reconstruction that depends on processing cadence.

Replay retains coordinate/pressure/tilt samples and both timing domains. It does not sleep between samples or reproduce device/Qt dispatch, display presentation, arbitrary key changes, or changes to tool settings. Use a fixed tool configuration when comparing geometry. This replay is a reconstruction diagnostic, not an end-to-end pen latency benchmark.

The loader rejects raw/viewer coordinates, nonfinite samples, invalid pressure, and backwards ordering ticks. It caps one replay at one million samples and reports rejected input through Qt's warning output.

## Generate a known circle

```bash
python tools/stroke_smoothness_diagnostics.py circle circle-50hz.csv \
  --rate 50 --duration 0.4 --radius 150
```

The generator emits `round(rate * duration) + 1` samples, including time zero, the full requested duration, and an exactly closed final position. This example produces 21 samples. The nominal rate is adjusted slightly when the requested duration does not contain an integer number of sample intervals.

The output is already in accepted/tool-coordinate replay format. Its center defaults to `(0, 0)` and can be changed with `--cx` and `--cy`.

## Analyze each track and stage

```bash
python tools/stroke_smoothness_diagnostics.py analyze stroke-accepted.csv

python tools/stroke_smoothness_diagnostics.py analyze stroke-geometry.csv \
  --stage reconstructed --center 0 0 --radius 150

python tools/stroke_smoothness_diagnostics.py analyze stroke-geometry.csv \
  --stage fitted_curve --center 0 0 --radius 150
```

Groups remain separate by their available session, stroke, root/output track, stage, coordinate space, and device/touch identifiers. A final record also closes a group part, protecting older captures with missing or reused IDs. Nonfinite numeric rows are rejected with a line/field diagnostic instead of silently bridging over the missing point.

All rows remain available to pressure and timing metrics, including pressure changes at a stationary position. Only geometry calculations collapse consecutive identical positions, preventing duplicates from hiding a local turning angle.

Circle analysis reports:

- Vertex RMS and maximum radial deviation.
- Maximum radial deviation over each logged line segment, including its closest interior point to the circle center.
- Median and maximum turning angle of consecutive distinct positions.
- Equal and backwards time pairs and median intervals, with nanosecond timestamps subtracted before conversion to floating point.

An explicit `--center CX CY --radius R` supplies a reference in the logged coordinate space. Without it, the circle is fitted to the observed vertices and labeled accordingly; that fit is not the artist's intended curve. Short/collinear groups report unavailable circle metrics rather than crashing.

The segment metric catches a regular radius-150 hexagon's approximately **20.096189432** units of interior radial error even though its vertices lie on the circle. `fitted_control` omits drawn-curve metrics; select `fitted_curve` to measure sampled curve geometry. For that stage, the segment metric evaluates the sampled chords, not an analytic maximum over the continuous quadratic curve.

## Initial evaluation

The first implementation intentionally uses narrow source-level checks: sample-clock/track invariants, streamed-versus-batched geometry, dab/invalidation contracts, preview vertex equivalence, and analyzer edge cases. These checks are described in the [implementation note](../doc/drawing_pipeline_exploration.md).

The initial change does not establish actual tablet latency, hardware pressure behavior, GPU pixel identity, or a measured frame-rate gain. The next practical evaluation is a short Windows build/pen smoke pass covering a circle, a fast corner, a stationary pressure change, a taper, and undo for each brush type. Keep trace recording off when measuring responsiveness.
