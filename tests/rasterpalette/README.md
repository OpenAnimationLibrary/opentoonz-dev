# Raster palette regression coverage

Run on Linux with Python 3, a C++17 compiler, pkg-config, Qt 5 Core/Gui/Network
development files, Boost headers, and LZ4 development files:

```sh
python3 tests/rasterpalette/run.py
python3 tests/rasterpalette/run.py --cxx clang++
```

The Ubuntu GCC and Clang jobs run this before the application build. Build
products and test files live in temporary directories. An optional
`--build-dir /absolute/path` retains build products for inspection.
`--include-root /absolute/path` supports development headers extracted under
`usr/include` when only the runtime libraries are installed (x86-64 Linux).
No test reads or writes the artist's project or Studio Palette folders.

The OT-Dev Windows job also enables `WITH_RASTER_PALETTE_SAVE_TEST` and runs
`native/native_save_test.cpp` after packaging. This executable links the actual
OpenToonz libraries and exercises global/project palette creation, overwrite,
native serialization/reload and temporary snapshot cleanup, with spaces,
parentheses and Unicode in the paths. It has no scene/project/stream fixtures.
It specifically covers the Windows CRT sharing conflict caused by reopening a
still-owned `QTemporaryFile` through `_wfopen_s`. The writer now uses a normal
file inside an automatically cleaned temporary directory instead.

To run that test in a developer build, configure with
`-DWITH_RASTER_PALETTE_SAVE_TEST=ON`, build `raster_palette_save_test`, and run it
with the built OpenToonz and Qt DLLs on PATH. It is off by default and is not
included in the application package.

## What runs

- `palette_io_test.cpp`: the production `FullColorPalette`, native palette
  serialization, style metadata, pages, animation, stream and filesystem code.
  Covers project/legacy/global precedence, editing without a drawing, project
  save and global default snapshots, lock/reference-frame preservation,
  malformed input, missing aliases, serialization failures, directory targets,
  and kernel-enforced short writes. Failed writes must preserve old file bytes,
  unsaved edits and directory contents.
- `application_test.cpp`: the current production bodies of
  `TApp::updateXshLevel`, `TPaletteHandle::setPalette`/`setStyleIndex`,
  `IoCmd::saveNonSceneFiles`/`saveSceneIfNeeded`, and both palette save command
  handlers. `run.py` extracts them from the checkout at each run. Covers early
  palette edits and color selection, empty columns, raster gaps, column/level
  guards, cleanup focus, save/cancel/error handling, project isolation,
  discarded edits, and indexed palette selection/save routing.

The test checks conditions with throwing `CHECK`s, including in release mode.
Failures return a nonzero exit status. The decision methods are not copied
into the fixture or rewritten as Python assertions.

## Boundaries and limitations

This is a focused native runtime harness, **not an application GUI test**.
Scene/project alias routing, levels, columns, resource enumeration, dialog
answers and UI notifications use small fixtures. Scene switches explicitly
detach the palette, clear the cache and select the next fixture scene; this
does not execute the complete New Scene/Load Scene/Change Project UI. The
Save All resource method is exercised, rather than the scene-save dialog.
Indexed level save destinations are observed as calls; actual TLV/PLI image
loading, writing and rendering are not covered.

For the Linux harness, the `FullColorPalette` header omits `Q_OBJECT` in a generated shadow header;
its QObject base remains. Rendering entry points and image codecs throw if
called. Generated copies of `tpalette.cpp` and `tcolorstyles.cpp` replace only
those entry points and use equivalent default construction for a null raster.
`rendering_stubs.cpp` supplies solid-color persistence without a GL renderer.
Production palette I/O, metadata, routing and save decisions remain unchanged.

## Interactive release checklist

Use disposable projects A and B with distinct raster palettes. Keep copies of
an existing Toonz Raster level and vector level (including animated/linked
styles). Record the build commit, platform and result for each row.

| Scenario | Required result |
| --- | --- |
| Raster default + Autocreation, empty column | Select/edit/add a color before the first stroke; first drawing uses it. No level is created merely by choosing the color. |
| Toggle Autocreation and default level type | Empty-column raster palette appears only for Raster with Autocreation on. |
| Gaps before/between/after raster exposures | Palette follows the preceding level, or first following level when none precedes; selected color survives. |
| Hidden/locked columns, read-only levels | No newly enabled drawing or palette behavior bypasses existing restrictions. |
| Save Palette and Save All before first drawing | Reopen/new scene in the same project restores palette edits, page/style metadata and colors. |
| Save As Default from Options and tab menu | Cancel preserves the previous file; confirm updates the global default, keeps project edits dirty, and leaves a project's own palette in precedence. |
| New/Load Scene and Change Project | Save, discard and cancel each behave correctly; canceled/failed saves retain edits; project A and B keep their own palettes. |
| Frozen palette viewer and cleanup focus | Commands target the intended live palette; cleanup focus remains correct; frozen views remain usable across scene changes. |
| Existing Toonz Raster and vector levels | Creation, color/style IDs, drawing, undo, Save Palette, Save All and reopening remain unchanged. Raster default command cannot overwrite their defaults. |

Automated passes do not mark these interactive rows as completed.
