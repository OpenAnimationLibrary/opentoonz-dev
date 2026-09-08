# OT-Dev Windows session recorder (experimental)

Only the OpenAnimationLibrary/opentoonz-dev Windows CI workflow injects this
feature. Normal source builds and upstream source files are unchanged. This is
not an OpenToonz movie renderer or a desktop recorder.

On startup choose **Start Recording** or **Do Not Record**. Escape/close means
no recording. **Remember my choice for this build** stores `BuildId` and `Choice`
under `[OtDevRecorder]` in the active portable `preferences.ini`. A new CI build
(including a rerun) asks again. `OTDEV_RECORD=off` suppresses startup recording
even after remembered consent; the toolbar can still be used to opt in later.
There is intentionally no command-line switch that bypasses consent.

After opt-in, continuous capture saves clips under
`<OpenToonz executable directory>/portablestuff/recordings/`. The bottom recording
toolbar shows actual recording/paused/error state, stops/resumes capture, and
opens the folder. Stopping updates a remembered choice to off. Nothing is
uploaded and no recordings are automatically deleted. The recorder stops with
a visible error when free space falls below 1 GiB. Files have collision-resistant
UTC names and are split every 10 minutes of encoded frames.

## Capture boundary and limitations

- Qt renders the main client area and visible Qt windows parented to it (including
  floating panels, menus and dialogs). OpenGL widget framebuffers are explicitly
  composed. Gaps between windows are black. No desktop pixels or OS window frames
  are read; unrelated applications, notification windows and native dialogs are
  not inputs. Qt windows without a main-window ownership chain are omitted.
- Native file dialogs, minimized/inactive application state, and unrecognized
  native foreground windows pause capture. In-app browsers, artwork, scene names
  and paths **can** appear in recordings. This is app confinement, not redaction.
- Output is fixed 1920x1080 at nominal 12 fps, letterboxed to the current UI extent.
  Resizing and DPI changes do not change raw-frame size. No audio is recorded.
  Captures run on the GUI thread; slow rendering or encoder backpressure can drop
  frames. Paused/dropped time is omitted, so this is not a timing/latency benchmark.
- MPEG-4 video in fragmented MP4 keeps completed fragments readable after abrupt
  termination, but the final fragment may still be lost. Stop and shutdown send
  EOF and allow a bounded finalization interval. Disk and encoder errors are shown.
- A pinned minimal FFmpeg n8.1.2 build has no network, capture-device, or audio
  inputs. It accepts raw video over a pipe and writes MP4. The package includes
  the matching FFmpeg source archive, LGPL text, license information, configuration
  output, and build recipe. It lives in `otdev-recorder`, separate from OpenToonz's
  existing user-configured FFmpeg export integration.

## Validation

`python -m unittest discover -s .github/ot-dev/recorder -p 'test_*.py' -v`

The standalone CMake/QtTest target in this directory tests consent persistence,
escape/opt-out, capture ownership/resize, and real encoder output. Set
`OTDEV_TEST_FFMPEG` to the packaged encoder before running CTest.

Windows CI builds and runs these tests before building OpenToonz. Interactive
acceptance in the extracted artifact must additionally check the Viewer, Style
Editor, Xsheet, FX Schematic, floating panels, popup menus, native file dialogs,
overlapping external windows, multi-monitor/high-DPI use, shutdown/cancelled quit,
and several minutes of recording/playback. CI alone does not establish visual
correctness or acceptable performance across Windows GPU drivers.

References: [Qt widget rendering](https://doc.qt.io/qt-6/qwidget.html#render),
[OpenGL framebuffer capture](https://doc.qt.io/qt-6/qopenglwidget.html#grabFramebuffer),
[FFmpeg formats](https://ffmpeg.org/ffmpeg-formats.html),
[pinned FFmpeg source](https://github.com/FFmpeg/FFmpeg/commit/38b88335f99e76ed89ff3c93f877fdefce736c13).
