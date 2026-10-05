# tcomposer worker diagnostics (version 1)

This is the first, local-worker foundation for selecting compatible render
workers and measuring a future EXR-master workflow. It does not change default
rendering, choose a different executable, or enable EXR post-processing.

## Probe a worker

Run `tcomposer --worker-info-json` with no scene argument. The program prints
exactly one compact JSON object on stdout and exits with status 0. This is an
early, static probe: it does not create a QApplication, look for TOONZROOT,
load plugins, initialize a GPU, or write a farm log. Example:

```json
{"build_abi":"x86_64-little_endian-lp64","features":{"scene_render":true,"timing_jsonl":true},"product":"OpenToonz","role":"tcomposer","schema_version":1}
```

`schema_version` describes the JSON contract, not rendering compatibility.
`build_abi` identifies the Qt build architecture but is not a scene/FX or
plugin ABI guarantee. `features` lists only implemented behavior. The future
Preferences picker must warn before executing a user-selected worker, then
compare its capabilities with the host, check the worker's matched runtime,
and offer a disposable render test. A filename such as `tcomposer_gpu.exe` or
`tcomposer_exr.exe` is only a label until the worker provides the relevant
implementation and verifiable runtime diagnostics.

## Record an opt-in render trace

Pass `-timing-jsonl path/to/render.jsonl` with a normal scene command. The
existing render/output options still determine the render. The sidecar is
created immediately before `generateMovie()`; it must end in `.jsonl` and
cannot overwrite an existing file. A path that cannot be opened fails before
frames start. With no flag, no sidecar is created and the usual
stdout and farm progress behavior stays in place.

Qt 5.11 and newer use an operating-system exclusive create for the sidecar.
On the older Qt versions accepted by the project, the pre-open existence check
does not protect against another process creating that path at the same moment.

Every line is an independent JSON object with `schema_version: 1` and a
monotonic `elapsed_ms` since the sidecar was opened. Events are:

- `render_started`: requested frame range, step, shrink, thread count, tile
  limit (`null` means no limit), output extension, and multimedia mode.
- `render_parameters`: effective clamped frame bounds and scene time stretch.
  Column rendering can skip a column on some frames.
- `frame_completed` or `frame_failed`: one-based frame number after the output
  callback; multimedia mode also includes the column. Callbacks can arrive in
  a different order than frame numbers, and a single render computation can
  cover multiple exposed frames.
- `render_finished`: counts reported by callbacks and a `completed` or
  `failed` status. A crashed or forcibly terminated worker can leave the trace
  without this event. If sidecar writes fail, the process exits nonzero. With
  this option selected, observed multimedia frame failures also exit nonzero.

The elapsed values are wall-clock observations of this worker's render phase,
not additive per-frame render or EXR-write durations. Scene loading and any
later FFmpeg conversion are outside this first trace. A future time planner
must instrument those stages separately and retain timings for the exact
worker, hardware, scene, and output recipe. Legacy multimedia rendering without
this flag can report a successful process exit after a column failure; this
flag detects the callback failure count.

For a packaged Windows build, run:

```text
python doc/tools/tcomposer_worker_probe_smoke.py path/to/tcomposer.exe
```
