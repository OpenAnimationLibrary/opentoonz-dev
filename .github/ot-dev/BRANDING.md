# OT-Dev build branding

## Splash designs

- `splash-template-assembly-line.svg`: the active assembly-line artwork.
- `splash-template-legacy.svg`: the preserved previous artwork.

Both are templates populated from current PR number/title, description, UTC
build date, and actual checkout commit. Branch builds show the ref instead.
The original seven fields remain supported. The new artwork adds wrapped
heading fields; XML escaping is applied once to metadata.

The Windows workflows explicitly select the assembly-line template. Branding
updates both window-title paths, the compiled splash, and the external splash
in the CI working tree. The generated external file is `ot-dev-splash.svg`.
The build-only startup overlay loads that distinct name, so a legacy
`splash.svg` left beside an updated executable cannot override the new artwork.
The compiled resource remains the fallback. The clean upstream source is unchanged.

Both `main` and `edition` need this infrastructure update for their ordinary
PR builds. Merging it into one branch does not update the other branch's builds.
Existing downloads must be rebuilt to pick up the new artwork.

## Branded builds of upstream-based PRs

The separate `otdev_branding.yml` workflow is maintained on `edition`. Create a
branch **from edition** named **otdev-build/pr-NUMBER** in GitHub's branch
picker; for example, `otdev-build/pr-259` builds open PR #259. A suffix such as
`otdev-build/pr-259-20261019` requests a fresh build with current metadata.
Branch creation and pushes trigger it. No extra PR is needed for build refs.

The job fetches current PR metadata and its test merge SHA, checks out that
exact source commit, and loads branding assets from its workflow revision.
It uploads a branded Windows application and a separate populated SVG.
A conflicted or closed PR is rejected. The PR source and clean mirror stay intact.

This infrastructure belongs only in OT-Dev build branches. Keep the templates,
workflows, and build-only source overlays out of upstream feature proposals.

Validation: `python -m unittest discover -s .github/ot-dev -p test_brand_build.py -v`.
Artwork concept: Rodney Baker. SVG drawing and workflow implementation assisted
by OpenAI Codex. Static lettering is outlined; current build metadata is SVG text.
