# Diagnostics for Drawing Stroke Smoothness

This document defines a deterministic diagnostic protocol for investigating fast-stroke angularity and related drawing-smoothness reports.

## Purpose

The goal is to separate three different classes of failure that can look similar in screenshots:

1. **Input starvation** — the device/Qt delivers too few samples, or OpenToonz gates/drops samples before the drawing pipeline receives them.
2. **Timing distortion** — samples arrive with spacing that differs from their source timestamps.
3. **Reconstruction failure** — sufficient input reaches the drawing pipeline, but interpolation/tangent reconstruction concentrates curvature into visible joints.

This companion work is intended to support investigation of upstream PR #7068 and similar reports without assuming a particular cause.

## Scope: what this work does

- Records raw tablet events before platform-specific move gating, including source timestamp, arrival timestamp, position, pressure, `isHighFrequent` state and whether the event is forwarded.
- Records the tool-coordinate events accepted by `TInputManager::trackEvent()`.
- Records final reconstructed output geometry after the modifier chain.
- Writes the three streams to CSV when `OPENTOONZ_INPUT_TRACE` is enabled.
- Replays accepted/tool-coordinate CSV streams incrementally through `TInputManager::trackEvent()` when `OPENTOONZ_INPUT_REPLAY` is set.
- Provides an analytic circle-event generator at a chosen sample rate, duration and radius.
- Provides a small analyzer that reports best-fit-circle and local-turning metrics.

## Scope: what this work does NOT do

This work intentionally does **not**:

- change brush smoothing;
- change `TModifierTangents` behavior;
- remove or alter the 20 ms tablet-event gate;
- change `isHighFrequent()` handling;
- change Qt tablet-event compression attributes;
- change WinTab or Windows Ink selection;
- change MyPaint behavior;
- claim that a tablet driver, event starvation, timestamps, or spline reconstruction is the root cause;
- attempt to fix PR #7068 directly.

The purpose is measurement and reproducibility. Behavioral fixes should be evaluated separately against a fixed captured or synthetic stream.

## Enable recording

Set the following before launching OpenToonz:

```text
OPENTOONZ_INPUT_TRACE=1
OPENTOONZ_INPUT_TRACE_DIR=<optional output directory>
```

If no output directory is supplied, files are written beneath the system temporary directory in `opentoonz-stroke-diagnostics`.

Each session creates:

```text
stroke-<session>-raw.csv
stroke-<session>-accepted.csv
stroke-<session>-geometry.csv
```

The raw file uses viewer/device coordinates and is intended for delivered-versus-forwarded analysis. The accepted file uses the coordinates actually supplied to `TInputManager` and is therefore the replay format.

## Replay a captured stroke

Set `OPENTOONZ_INPUT_REPLAY` to an **accepted** CSV file before launching OpenToonz:

```text
OPENTOONZ_INPUT_REPLAY=C:\tests\stroke-accepted.csv
```

Select a drawing tool and begin one stroke. The first incoming track event is replaced by the stored stream, which is injected point-by-point through `TInputManager::trackEvent()` and processed after each sample. This preserves incremental modifier/fitter behavior while avoiding dependency on the original tablet or driver.

Raw pre-gate CSV files are intentionally rejected for replay because their positions are viewer/device coordinates rather than tool coordinates.

## Generate a synthetic circle

```text
python tools/stroke_smoothness_diagnostics.py circle circle-50hz.csv --rate 50 --duration 0.4 --radius 150
```

The generated file is already in the accepted/tool-coordinate replay format and uses nanosecond `TToolTimer` spacing.

## Analyze a stream

```text
python tools/stroke_smoothness_diagnostics.py analyze stroke.csv
```

The analyzer reports:

- row/accepted count and acceptance ratio when `forwarded` is present;
- best-fit-circle center and radius;
- RMS and maximum radial deviation;
- median and maximum local turning angle.

## Deterministic comparison protocol

For each build under comparison:

1. Replay the same accepted event stream, or generate the same analytic circle.
2. Record accepted input and reconstructed geometry.
3. Compare sample count, radial error and maximum local turning angle.
4. Separately compare the raw and accepted recordings from physical-tablet tests to determine whether events were lost before `TInputManager`.

This keeps input starvation and reconstruction failure measurable as separate problems instead of judging both from screenshots alone.
