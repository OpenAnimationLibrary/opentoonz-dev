# Qt 5.15.2 PDF dependency

OpenToonz Development uses the custom Qt 5.15.2 WinTab build published by
`shun-iwasawa/qt5`. The manual `Build Qt 5.15.2 PDF dependency` workflow adds
the Qt PDF modules from the matching Qt 5.15.2 source release and emits one
versioned replacement archive.

## Included scope

The archive retains the complete existing WinTab Qt SDK and adds both release
and debug variants of Qt PDF. It includes:

- `Qt5Pdf` and `Qt5PdfWidgets` runtime and development files;
- the Qt Quick PDF module installed by the upstream build;
- CMake and qmake package metadata, public headers, and import libraries;
- PDFium's optional V8 and XFA support, including BMP, GIF, PNG, and TIFF XFA
  image decoders; and
- the applicable Qt and Chromium license notices plus an input/feature
  manifest.

No Qt PDF or PDFium feature exposed by the Qt 5.15.2 build is deliberately
disabled. Qt PDF is nevertheless a renderer, not a complete PDF editor. Its
format and security support is bounded by the PDFium revision shipped in Qt
5.15.2. Future security fixes may therefore require a new package version.

## OpenToonz integration

PDF import only needs the `Qt5::Pdf` target and `QPdfDocument`: each selected
page is rendered to a raster frame at the DPI chosen by the user. The broader
SDK payload is packaged now so later OpenToonz work can use search, bookmarks,
navigation, Widgets, or Qt Quick PDF without rebuilding this dependency.

Linking a new module still requires an OpenToonz application update. Runtime
deployment remains automatic through `windeployqt`; only modules linked by the
application are placed in an end-user OpenToonz package.

## Reproducibility and release

The workflow pins and verifies SHA-256 hashes for both its inputs. After a
successful manual build, promote the artifact to an OpenAnimationLibrary-owned
release and change `workflow_windows.yml` to download that immutable URL and
verify its checksum. Do not point normal builds at a temporary Actions artifact.
