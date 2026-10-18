# Qt6 readiness reports

OT-Dev records Qt6 porting findings while development continues with Qt5.
The workflow uses pinned Clazy checks against the Linux OpenToonz target and
its dependencies. A completed report is not a Qt6 build or a cross-platform
compatibility verdict; review the findings before changing application code.

## Automatic coverage

- Pull requests containing this workflow are checked when opened, reopened,
  or updated. There is no changed-file filter.
- Every push to OT-Dev `main`, including merged PRs and upstream integrations,
  records the integrated code.
- Upstream-based proposals may not contain this OT-Dev workflow. After their
  existing **Linux Build** workflow completes, a separate readiness run analyzes
  that build's head commit. This also covers Linux builds following updates to
  the clean `upstream/master` mirror. The Linux build need not succeed for the
  report to run, but it must have been triggered; its own file filters, fork
  approval requirements, and merge-conflict restrictions still apply.

The normal PR check analyzes the PR merge commit. Linux-build follow-ups record
the triggering head commit and link to the originating build. Superseded normal
PR runs are cancelled; pushes to `main` each retain their own run.

The manual **Run workflow** option remains available once this workflow is
merged into the repository's default branch. Choose a branch containing the
workflow to run it again.

## Finding the summary and logs

Open [Actions → Qt6 Readiness Report](https://github.com/OpenAnimationLibrary/opentoonz-dev/actions/workflows/workflow_qt6_readiness.yml)
and select a run. Its **Summary** displays the analysis status, source commit,
unique diagnostic counts, and up to 40 review candidates. Each run also uploads
a downloadable artifact named `qt6-readiness-report`:

| File | Contents |
| --- | --- |
| `qt6-readiness.md` | Copy of the readable report |
| `build.log` | Full compiler output and all emitted porting diagnostics |
| `toolchain.txt` | Installed Clazy, Clang, and Qt package versions |
| `checks.txt` | Checks supported by the installed Clazy version |

Artifacts are retained for **90 days**. Download an artifact before it expires
if its full log needs to be preserved longer. Setup failures may leave some
files absent; the summary marks analysis incomplete when compilation did not
finish successfully. An incomplete report is not evidence of compatibility.

This directory stores the guide. Per-run reports and compiler logs are stored
as GitHub Actions results rather than committed into the source tree.

## Advisory behavior and retirement

Porting findings are warnings and do not make a successful compilation fail.
Toolchain, configuration, compilation, and upload failures remain visible as
workflow failures. Keep this report separate from required release checks.

Follow-up builds have read-only repository permissions, do not persist checkout
credentials, reference no repository secrets, and share no build caches.
These restrictions must be preserved because proposal build scripts execute.

Keep the workflow active through the Qt6 transition. When maintainers decide it
is no longer useful, disable **Qt6 Readiness Report** in Actions, or remove the
workflow and update this guide in a focused PR. There is no automatic end date.

Reference: [Porting to Qt 6 using Clazy](https://doc.qt.io/qt-6/porting-to-qt6-using-clazy.html).
