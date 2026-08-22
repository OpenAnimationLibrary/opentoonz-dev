# Issue #242 indexed PNG investigation

This branch is for investigating upstream OpenToonz issue [#242](https://github.com/opentoonz/opentoonz/issues/242), where 8-bit indexed/paletted PNG images have historically loaded with corrupted/glitchy output or, in some variants, crashed.

## Current observations

- Upstream issue #242 is still open and was reopened.
- The historical diagnosis points at the native PNG reader and specifically palette expansion / metadata after libpng transforms.
- The current `tiio_png.cpp` already calls `png_set_palette_to_rgb()`, `png_set_tRNS_to_alpha()` when applicable, `png_read_update_info()`, then refreshes `m_channels` and `m_rowBytes`.
- A user-supplied sky image reported to reproduce the problem visually should be tested in the actual OpenToonz import path on Windows, Linux and macOS.
- The copy supplied in ChatGPT decoded as a normal RGBA PNG rather than palette mode, so the original indexed source or a freshly generated palette-preserving equivalent is needed for a precise regression fixture.

## Investigation plan

1. Reproduce with an actual `PNG_COLOR_TYPE_PALETTE` file (8-bit indexed) with and without `tRNS` transparency.
2. Compare native OpenToonz PNG import against a reference decoder.
3. Verify `m_bit_depth`, `m_color_type`, `m_channels`, `m_rowBytes`, `m_info.m_bitsPerSample`, and `m_info.m_samplePerPixel` before and after `png_read_update_info()`.
4. Check whether `png_set_filler(..., 0xFF, PNG_FILLER_AFTER)` combined with `png_set_tRNS_to_alpha()` produces an invalid transform sequence for paletted PNGs carrying `tRNS`.
5. Add a regression test/fixture once the exact failing palette encoding is confirmed.
6. Keep the fix limited to PNG decoding unless evidence shows a broader raster import assumption.
