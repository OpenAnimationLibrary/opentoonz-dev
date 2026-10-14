# Trail Settings regression tests

The default configuration builds only the dependency-light gesture and click
carrier tests. The real raster/TLV source test is opt-in and uses actual readers,
writers and the matching OpenToonz core, not mocked image formats.

## Real raster and TLV sources

Build `tnzcore` from the same checkout/configuration as the tests. Supply Qt 5,
PNG, JPEG, **OpenToonz's patched TIFF**, and the two LZO helper executables.
The native TIFF reader requires `TIFFReadRGBATile_64` and
`TIFFReadRGBAStrip_64`; a stock system TIFF does not provide these extensions.
The bundled patched TIFF is in `thirdparty/tiff-4.0.3`.

Example after building/installing that TIFF into `/absolute/path/to/tiff`:

```sh
cmake -S toonz/tests/trail_settings -B build/trail-source-tests \
  -DCMAKE_BUILD_TYPE=Release \
  -DTNZCORE_LIBRARY=/absolute/path/to/libtnzcore.so \
  -DBUILD_TRAIL_SOURCE_TESTS=ON \
  -DTIFF_INCLUDE_DIR=/absolute/path/to/tiff/include \
  -DTIFF_LIBRARY=/absolute/path/to/tiff/lib/libtiff.so \
  -DTIFF_LIBRARY_RELEASE=/absolute/path/to/tiff/lib/libtiff.so \
  -DTRAIL_LZO_BIN_DIR=/absolute/path/to/lzo/helpers
cmake --build build/trail-source-tests --target trailsource_test
ctest --test-dir build/trail-source-tests --output-on-failure -R '^trailsource$'
```

Use matching platform library paths, Qt `CMAKE_PREFIX_PATH` where needed, and
`--config Release` / `-C Release` with multi-configuration generators. Rebuild
all relevant binaries together: mixing Debug/Release or old/new class layouts
is not supported. Make shared dependencies discoverable through the usual
platform loader paths.

The test covers real PNG, TIF/TIFF, BMP, JPG/JPEG sequences; uppercase PNG/TIF;
a three-frame TLV/TPL pair including a blank drawing; palette-sidecar rejection;
source resolution and filters; ink/paint, antialiasing, opacity, palette-animation
isolation; and grayscale/16-bit/float normalization. It does not open the Style
Editor GUI or validate OpenGL Viewer/Preview/final-render parity.
