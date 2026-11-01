# OT-Dev Windows build branding

OT-Dev Windows application packages include the splash, live PR/build information
in the title bar, and the session recorder. The overlay applies only in Windows
CI; Linux, macOS, clean upstream source, and standalone CLI/test builds keep
their existing behavior.

## Replace the artwork without editing the SVG

| Package path | Purpose |
| --- | --- |
| `OpenToonz.exe` | Application with the title-bar label and recorder |
| `splash.svg` | Stable layout with populated PR/build text |
| `portablestuff/splash.png` | Replaceable artwork at the root of `portablestuff` |
| `otdev-recorder/` | Encoder, matching sources, licenses, and README |
| `otdev-build.json` | Verified source, infrastructure, and package identity |

Replace **`portablestuff/splash.png`** with a valid PNG and restart OpenToonz.
No SVG edit or rebuild is needed. The preference-aware loader uses the active
stuff folder, so installed builds, portable builds, and `-TOONZROOT` overrides
use their own `splash.png`. The SVG retains its fixed image reference for older
builds. The launch working directory does not affect it. CI copies the PNG after the
ordinary `stuff` files so those files cannot overwrite the selected artwork.

Alternatively, select **Preferences → Interface → Splash Screen Image** to use
any PNG without moving or renaming it. **Use Default** clears that preference.
**Help → About** uses the same local splash and fallback policy. Reopen About to
see preference changes or replaced artwork immediately; the startup splash
updates on the next launch. The selected file is referenced, not copied.

The local template remains `splash.svg` beside the executable. The loader uses
its `<image id="artwork">` slot, retaining its size, aspect-ratio policy, text,
and build information. `id="splash-artwork"` is also supported. Existing custom
SVGs without a marked image still render as-is; add the ID to the intended image
once to make their artwork replaceable. The built-in upstream layout also has a
slot above its footer, leaving its original vector design visible without a PNG.

| Template | PNG lookup order |
| --- | --- |
| Readable local `splash.svg` | Preference, then active stuff folder's `splash.png`, then the SVG's own content |
| Compiled `:Resources/splash.svg` when local SVG is missing/invalid | Preference, stuff PNG, executable-adjacent `splash.png`, resource-adjacent `:Resources/splash.png`, then embedded SVG content |

Missing, unreadable, corrupt, and non-PNG artwork is skipped. A missing PNG does
not discard a valid local SVG. A malformed local SVG falls back to the compiled
template. No persistent SVG edits are made when substituting artwork. The
compiled SVG is inside the application rather than an editable disk folder;
the executable-adjacent PNG is the convenient disk override for that fallback.

For future builds, replace `.github/ot-dev/ot-dev-splash-artwork.png`; CI installs
it as `portablestuff/splash.png`. Edit `splash-template-area10.svg` only when
changing the layout or templating. The original artwork was supplied by Rodney
Baker. It appears above the metadata footer without cropping, within 610 by 480
logical pixels.

CI populates the PR number/title, description, UTC date, and actual compiled
checkout commit. Branch builds identify their ref. All seven original fields
and the two wrapped heading fields remain available. XML escaping happens once.
The compiled splash embeds the same PNG and metadata as a self-contained
fallback when the external template is missing. Replacing the external PNG
retains the SVG's build information. Older downloads need a fresh build to
acquire this layout.

Preserved designs remain in `splash-template-assembly-line.svg` and
`splash-template-legacy.svg`, renderable explicitly with `--template` for
reference. Complete Windows packages require the active template and fixed PNG
location. Keep these OT-Dev assets out of upstream feature proposals.

## Automatic Windows coverage

`workflow_windows.yml` applies the full overlay directly when its branch
contains this infrastructure. Existing PR-specific tests still run. After
copying the application and `stuff`, `package_build.py` verifies the compiled
title, build ID, recorder, SVG/PNG, and encoder distribution before publication.

**OT-Dev Windows Package** (`otdev_branding.yml`) also follows successful
completion of the existing application workflows:

| Source workflow | Coverage |
| --- | --- |
| `Windows Build` (`workflow_windows.yml`) | Clean upstream-based PRs, older main/Edition PRs, and branch builds |
| `OT-Dev Branded PR Build` (`otdev_branding.yml`, older revisions) | Existing dedicated PR build refs |
| `Channel Stack Preview (Windows)` (`channel_stack_preview.yml`) | The existing combined channel preview |

The source workflow performs its own validation first. If it publishes both a
new-format complete application package and
`OT-Dev-package-manifest-SOURCE_RUN_ID`, the follow-up skips the duplicate build.
Otherwise it builds a complete OT-Dev package from that successful run's exact
application source, with build infrastructure checked out separately. Old
splash-only artifact names do not count as completion.

For PRs, the resolver checks the artifact's source SHA against the current test
merge and its exact base/head parents. Closed, conflicted, stale, or ambiguous
PR runs are skipped. A fresh source run or manual request builds the current PR.
Branch and channel-stack builds retain their source ref and commit instead of
being presented as a single PR. The **OT-Dev Windows package** commit status
links to the follow-up result.

Download the complete application artifact:

- `OT-Dev-PR-N-Windows-SOURCE_SHA-RUN_ID`
- `OT-Dev-Branch-REF-Windows-SOURCE_SHA-RUN_ID`

Ordinary branch builds use `Branch-build` in the name and record the actual ref
in `otdev-build.json`. Encoder, branding, and manifest artifacts are supporting
files. Original source-validation runs and artifacts remain available;
historical downloads are not rewritten. Existing PR heads, bases, and
specialized tests are preserved.

### Default-branch requirement

Maintain this infrastructure on `edition` and make `edition` the repository
default **after merging and validating the workflow**. GitHub starts a
[`workflow_run` listener](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#workflow_run)
only when its file exists on the default branch. Merging it into a non-default
branch alone does not activate automatic coverage.

Keep `upstream/master` an exact upstream mirror. New feature proposals still
choose `upstream/master` explicitly as their base, as described in
`CONTRIBUTING.md`; changing the default does not retarget existing PRs. Updating
`main` with this infrastructure avoids a follow-up rebuild, but the listener
also covers its older workflows.

Compilation and recorder-test jobs use read-only repository permissions,
persist no checkout credentials, and receive no repository secrets. They run
separately from jobs that publish status. The new worker name differs from the
observed source names, preventing a loop.

## Manual requests and infrastructure validation

Use **Actions → OT-Dev Windows Package → Run workflow**, select the infrastructure
branch, and supply exactly one input: `pr_number` for an open OT-Dev PR, or `ref`
for a branch/commit.

The existing build-ref route also works. Create a branch from the desired
infrastructure revision named `otdev-build/pr-NUMBER`, optionally with a suffix
such as `otdev-build/pr-279-branding-test`. Creation and pushes resolve the PR's
current merge separately from the build-ref commit. This tests unmerged
infrastructure without changing PR source. The unused reusable-workflow entry
point is removed; use these explicit request routes.

## Verification

Run the tests with Qt 5.15.2 and CMake available:

```sh
python -m pip install PyQt5==5.15.2
QT_QPA_PLATFORM=offscreen python -m unittest discover -s .github/ot-dev -p 'test_*.py' -v
python -m unittest discover -s .github/ot-dev/recorder -p 'test_*.py' -v
cmake -S toonz/sources/toonz/tests -B splash-tests -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build splash-tests --config Release
ctest --test-dir splash-tests -C Release --output-on-failure
actionlint .github/workflows/workflow_windows.yml .github/workflows/otdev_branding.yml
```

The Qt regression renders from an unrelated working directory, replaces only
the PNG, and verifies that the artwork changes while the SVG bytes and rendered
metadata stay unchanged. It also renders the embedded fallback without the
external PNG. Resolver tests cover source identity, stale merges, pagination,
and duplicate prevention. Package tests reject incomplete branding/recorder
distributions. Recorder tests check external paths with the real CMake parser.

The standalone C++ splash regression exercises the actual loader and compiled
resource. It covers preference clearing, corrupt and missing PNGs, SVG fallback,
relative references from another working directory, Unicode paths, PNG alpha,
proportional fitting, unchanged footer text, and replacement at the same path.
The Windows application workflow runs it with the branded resource before the
full application build.
On Windows it uses the native Qt platform so SVG text resolves system fonts;
other platforms use the offscreen backend. Failure diagnostics are written to
CTest's captured stderr, including Qt initialization messages.

Windows CI additionally compiles and tests recorder consent, capture, encoding,
and native playback, builds OpenToonz, checks its compiled markers, and launches
the packaged encoder. Recording retains its existing consent behavior.

Artwork and requirements: Rodney Baker. Existing SVG/build work: OpenAI Codex
assistance. This Windows coverage and replaceable-PNG update: GPT-6 Astra Pro
assistance.
