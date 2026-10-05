# Improve UI Translation

This OT-Dev change addresses the open translation reports
[opentoonz#3873](https://github.com/opentoonz/opentoonz/issues/3873),
[opentoonz#7165](https://github.com/opentoonz/opentoonz/issues/7165),
[opentoonz#7166](https://github.com/opentoonz/opentoonz/issues/7166), and
[opentoonz#7167](https://github.com/opentoonz/opentoonz/issues/7167).

## 1. Translate displayed labels while preserving original values

Common font style names have a shared `FontStyleNames` translation context.
Lookup matches whole names, with a small explicit alias list. Unknown names
remain as supplied by the font. Full phrases such as "Bold Italic" have their
own entries; translated fragments are not concatenated. Choices that translate
to the same label include the original face name to distinguish them.

The Type Tool property keeps the original name in its enum range and uses its
existing UI-name support for display. The text-history and FX font controls
keep original names in combo item data. History settings, font selection, and
rendering continue to use those original values. The history list and tooltips
show translated names without modifying existing `typetool.ini` entries.

File Browser nodes have separate display labels. Only explicitly designated
special nodes are translated; actual folder/project names and node identifiers
remain unchanged. Translation is performed when the model requests a display
name, so nodes created before translator installation do not freeze English
labels. Edit roles and path lookup still use original names.

Project root captions in the project and scene-export dialogs use a complete
"Project root (%1)" phrase. The supplied path is inserted unchanged. The main
browser's existing `*folder (path)` root naming remains intact. The related
Scene Folder caption is also covered.

## 2. Repair the reported catalog gaps

The current Timeline code already exposes the reported strings through the
proper translation hooks. The current Toonz Raster Brush also exposes its
controls in the `ToonzRasterBrushTool` context. The repairs therefore fill
unfinished/missing entries rather than duplicating those hooks or restoring the
obsolete `BrushTool` context.

Targeted coverage includes:

- 28 common font style phrases and eight File Browser location captions.
- Both reported Timeline tooltips and the Window menu's `&Timeline` entry.
- Every currently active string in `ToonzRasterBrushTool::updateTranslation()`.
- Standard Qt dialog buttons in an OpenToonz-owned `qt_ui` catalog.

All eleven source-language catalog sets are checked. Existing finished wording
in the affected contexts is retained. Norwegian Bokmal remains a source catalog
set under development; this PR does not add it to the release language list.
Completing these issue-specific entries does not imply that the entire UI is
fully translated in each language. The audit also reports the wider backlog.

Missing locale metadata is restored in French catalogs and the German image
catalog. Incorrect Spanish locale metadata in the Portuguese image catalog is
corrected. Region-neutral metadata remains valid where it was already present.

## 3. Build and load the correct catalogs

The production CMake translation rule now declares each `.qm` as an output with
its `.ts` as a dependency. A translation-only edit, or a deleted `.qm`, triggers
regeneration even when the corresponding C++ module does not relink. Each
language/module pair uses its own temporary output, which is renamed only after
successful compilation. All commands quote paths, including Unicode paths and
paths containing spaces.

The application reads the selected catalog's language metadata for Qt's locale.
Older Qt versions and missing catalogs have a fallback mapping for the existing
release language names. English does not inherit the OS's language. Missing
application catalogs are reported in the application log without adding startup
dialogs.

`qt_ui.qm` ships alongside the application catalogs and provides independently
authored standard dialog-button labels. This avoids bundling third-party Qt
translation binaries under a different license. A complete Qt catalog can still
be loaded from the selected language directory, the executable's `translations`
directory, or Qt's translation directory, using the selected UI locale and its
normal language fallback. If present, it overrides the small button catalog.
Other Qt widget text can fall back to English if no complete Qt catalog is
installed. Native OS dialogs may continue to follow platform language settings.

Changing the UI language still requires the existing application restart.

## 4. Regression coverage

The standalone Qt test project does not require OpenToonz's rendering libraries:

```sh
python3 doc/tools/audit_ui_translations.py
cmake -S toonz/sources/toonzqt/tests/ui_translation -B build-ui-translation -G Ninja
cmake --build build-ui-translation
ctest --test-dir build-ui-translation --output-on-failure
```

It compiles the affected catalogs and the production UI translation helper.
Offscreen Qt tests exercise translated selections, duplicate labels, unknown
faces, unchanged INI values and rendered glyphs, missing-face fallback, empty
lists, preserved Unicode paths, standard dialog buttons, and a UI language that
differs from the default Qt locale. Catalog lookup tests check the compiled
Timeline and brush entries in every source-language set.

Separate tests verify no-op builds, `.ts`-only updates, deleted outputs, failed
compilation preserving the last valid catalog, and recovery after invalid XML.
Audit regression tests deliberately introduce missing/unfinished entries,
incorrect locale metadata, missing menu mnemonics, and invalid placeholders.

The dedicated UI Translation workflow runs on PRs and main pushes. It fails on
issue-scope coverage errors, invalid XML/locale metadata, or placeholder errors
introduced in other contexts compared with the PR base. Existing unrelated
unfinished entries and placeholder problems are reported in a JSON artifact.
This complements the full Windows application build; it does not depend on the
Linux/macOS application workflows enabling `WITH_TRANSLATION`.

### Manual application acceptance checks

- In Spanish and another language, verify the Type Tool, editable text history,
  and FX font dropdowns. Change family, restore history, and reopen the application.
- In the File Browser, navigate special locations, ordinary folders named
  "Library" or "History", and project roots with non-ASCII paths. Confirm that
  actual paths and editable names remain unchanged.
- Check the Timeline menu and both tooltips in Xsheet and Timeline orientations.
- Switch between Toonz Raster, vector, and full-color brush targets, including
  named presets and the custom preset; confirm the correct localized controls.
- Test an installed and a portable build with the OS language different from
  the selected OpenToonz language, including English. Check standard dialogs and
  verify the built catalogs are present in `stuff/config/loc/<language>`.

## Provenance and review

Rodney Baker requested the four-stage scope. The font-name lookup approach follows
Shun Iwasawa's suggestion in
[the #3873 discussion](https://github.com/opentoonz/opentoonz/issues/3873#issuecomment-816348173).
The issue reports originated with gab3d; Rodney brought the older reports into
the current issue list. Existing OpenToonz translation contributions are retained.

Implementation, tests, and new translation wording were prepared with OpenAI
Codex assistance. The exact backend model version is not exposed in this session.
New wording has been checked for technical integrity and consistent use, but has
not had native-speaker review. Such review is recommended before upstream
promotion, particularly for typography terminology. All newly authored files and
wording follow the repository's existing license; no third-party translation
binaries or copied toolkit translation wording are added.

Full Windows/macOS application testing and native-speaker wording review must be
reported separately from the standalone Qt checks.
