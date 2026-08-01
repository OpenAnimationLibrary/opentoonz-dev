# Port workspace: Tahoma2D PR #1471 — Onion Skin Opacity control

Upstream pull request: https://github.com/tahoma2d/tahoma2d/pull/1471

Original author: @manongjohn

Upstream commits:

- `e519635f45e36ea5ed76ebda36b56063c08ef5b0` — Onion Skin Opacity control
- `8bf7d3766e9f79a6c84c8d8e100657fa10e6bbf2` — Prioritize higher opacity with overlapping onion-skin markers

Target base used to start this port:

- `OpenAnimationLibrary/opentoonz-dev@93591170f157640cb7b1f17f2678394d1c2af4f6`

## Current status

The exact upstream mail-formatted patch is retained in `upstream.patch`, including the original author and commit metadata. It has **not** been applied wholesale because the Tahoma2D files contain fork-specific code that must not be copied into OpenToonz.

This draft is therefore a porting workspace, not a build-ready implementation yet.

## Required OpenToonz adaptations

1. Apply the feature hunks to the current OpenToonz files rather than replacing files from Tahoma2D.
2. Do not import Tahoma2D-only `OnionSkinMask` state such as `m_everyFrame` where it is absent from OpenToonz.
3. Keep `isMos()` and `isFos()` const-correct. The upstream patch makes them non-const only to simplify iteration; that is not required.
4. Correct the upstream marker-removal logic before porting. In `setMos()` and `setFos()`, removal must verify `iterator->first == requestedKey`; otherwise disabling a missing marker can erase the next marker.
5. Initialize `OnionSkinPopup::m_initializing` explicitly.
6. Replace deprecated Qt layout `setMargin()` calls with `setContentsMargins()` for the current OpenToonz codebase.
7. Review the added `xshcolumnviewer.cpp` call to `onSliderReleased()`. It changes Column Transparency popup behavior and should remain only if required by this feature.
8. Preserve existing OpenToonz rendering, light-table, Shift and Trace, and onion-skin behavior while introducing the per-marker fade value.

## Validation checklist

- Fixed and relative markers can each use Auto or a custom value.
- Overlapping fixed/relative markers choose the more visible result as intended.
- Removing a nonexistent marker does not remove a neighboring marker.
- Xsheet and Timeline popup placement/orientation are correct.
- Vector, Toonz Raster, Raster, mesh/plastic deformation, and OpenGL drawing paths agree.
- Shift and Trace, Guided Drawing, whole-scene onion skin, and light-table behavior do not regress.
- Existing scenes load without altered onion-skin marker behavior.
- Windows, macOS, and Linux builds compile cleanly.

## Reproducing the source commits locally

```bash
git remote add tahoma2d https://github.com/tahoma2d/tahoma2d.git
git fetch tahoma2d e519635f45e36ea5ed76ebda36b56063c08ef5b0 8bf7d3766e9f79a6c84c8d8e100657fa10e6bbf2
git cherry-pick -x e519635f45e36ea5ed76ebda36b56063c08ef5b0
git cherry-pick -x 8bf7d3766e9f79a6c84c8d8e100657fa10e6bbf2
```

Resolve conflicts by porting the individual feature hunks according to the notes above; do not accept complete Tahoma2D versions of the affected files.
