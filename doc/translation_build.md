# Translation build dependencies

This repair concerns builds configured with `WITH_TRANSLATION=ON`. Builds with
`WITH_TRANSLATION=OFF` do not generate translation targets or process `.ts`
files through these rules. They may still package prebuilt catalogs supplied by
another process. A successful translation-disabled build is not a translation
validation result. The documented Xcode workaround remains until tested there.

## Normal builds

Translation-enabled application targets compile committed `.ts` sources to `.qm`
outputs with `lrelease`. Editing a catalog or deleting an output triggers its
regeneration without requiring C++ relinking. Unchanged catalogs are not rewritten.
Compilation failure stops the build and preserves the last valid runtime output.
Temporary files are isolated by build directory, configuration, language, and
module before being renamed into the existing runtime destination.

The production language list and packaging destinations remain unchanged. For a
conventional `toonz/build` layout, runtime outputs are in
`stuff/config/loc/<UI language name>/<module>.qm`. Install/portable packaging must
copy those outputs to the configuration directory the executable actually uses.
This change does not make separately configured builds sharing one destination
safe to run while catalog sources are being edited or extracted concurrently.

## Explicit extraction remains available

`translate_update_toonz`, `translate_update_tnztools`,
`translate_update_toonzqt`, `translate_update_toonzlib`,
`translate_update_tnzcore`, `translate_update_colorfx`, and
`translate_update_image` retain their names. For example, from the repository root:

```sh
cmake --build toonz/build --config Release --target translate_update_toonz
```

Use the configured directory/configuration. In Visual Studio, build the matching
project explicitly. This invokes `lupdate` for the module's registered languages
and then compiles the historical intermediate files in
`<build>/loc/<source-language>/<module>.qm`. Run the normal application build
afterward to refresh runtime catalogs. Review `.ts` changes before committing.

These targets are deliberately outside the normal build dependency graph. `.ts`
files are never declared generated outputs, so ordinary builds and clean targets
do not invoke extraction or remove maintained catalogs. Explicit extraction uses
the module's existing source/header lists, excluding generated MOC/resource files.

Qt Linguist editing/Release, direct `lrelease`, and
`bash toonz/sources/translations/update.sh LANGUAGE` remain available. The script
is unchanged; its existing argument/error-checking limitations still apply.
Translators can submit editable `.ts` files without building the application.

## Focused regression

The fixture includes both production functions together, rather than testing
compilation without the extraction rules. It needs CMake, Python 3, and Qt tools;
it does not compile OpenToonz. For Ninja, from the repository root:

```sh
python3 toonz/cmake/tests/test_translations.py --lupdate /PATH/TO/lupdate --lrelease /PATH/TO/lrelease
```

For Windows, specify `--generator "Visual Studio 17 2022"` and the Qt executable
paths. CI runs Linux Qt 5/Ninja and Windows Qt 5.15.2/MSBuild checks. It exercises
normal/no-op builds, `.ts`-only edits, deleted outputs, source edits without
extraction, invalid XML with preservation/recovery, explicit extraction and
intermediate compilation, cleaning, translation-disabled builds, and paths with
spaces/non-ASCII characters. Full application packaging and Xcode remain separate
acceptance checks. Runtime Qt language loading, catalog wording, and locale
metadata are outside this repair.
