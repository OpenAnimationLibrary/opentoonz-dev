# JPEG XL support (exploratory)

This integration uses the external tools from [libjxl](https://github.com/libjxl/libjxl)
to read and write JPEG XL images. An optional experimental route also uses those
tools for other supported raster formats. The tools are installed separately;
OpenToonz does not acquire a build-time dependency on libjxl.

## Setup

1. Install a matched set of `cjxl`, `djxl`, and `jxlinfo` from the
   [official project releases](https://github.com/libjxl/libjxl/releases) or your
   operating system's package manager. Keep any required runtime libraries with
   the executables.
2. Open **Preferences > Decoder/Encoder**.
3. Set **JPEG XL Tools Path** to their directory, for example `C:/libjxl/bin`.
   Windows executables have the `.exe` suffix. This setting identifies the tools
   directory, not a library DLL or a single executable.
4. Set **JPEG XL Timeout (seconds)** if needed. The default is 600 seconds for
   each tool invocation.

The path is read when an operation starts, including in `tcomposer`. Changing it
does not require restarting OpenToonz. Format entries remain visible when tools
are unavailable, and attempting to use them reports the missing configuration.
A relative tools path is resolved from the application executable's directory,
which also supports portable installations.

## JPEG XL images and numbered sequences

`.jxl` always uses the external tools. The implementation supports individual
images and the normal OpenToonz numbered-image sequence workflow.

The output format options provide:

- **Bit Depth:** 8 or 16 bits per channel.
- **Alpha Channel:** preserve transparency or use the existing OpenToonz
  background handling.
- **Effort:** encoder effort from 1 to 9; the default is 7.

Encoding is explicitly lossless. Every intermediate JPEG XL conversion also
uses lossless settings. This describes compression of the supplied samples;
the usual OpenToonz raster conversions, alpha association, and selected output
bit depth still apply.

The raster bridge accepts single SDR/sRGB images with integer samples up to
16 bits, RGB or grayscale color, and optional straight alpha. It checks source
metadata before accepting a conversion. Animated JXL, floating-point/HDR data,
unrecognized color profiles, premultiplied JXL alpha, and additional channels
are reported as unsupported by this bridge. They are not silently reduced to
the first frame or an 8-bit raster.

OpenToonz DPI is written as standard TIFF resolution properties in a JPEG XL
XMP container box and read back on import. This is not general preservation of
all source Exif/XMP metadata. Files without recognized resolution metadata use
the normal OpenToonz level-resolution behavior.

## Experimental processing of other formats

**Use JPEG XL tools for other formats (Experimental)** is off by default.
It does not control `.jxl` processing.

When enabled, the adapter checks which codecs the selected tools actually
contain. A format listed in libjxl's documentation is not necessarily included
in every binary package.

| Format | Experimental behavior |
| --- | --- |
| PNG | Try a lossless conversion through JXL. Preserve native DPI and alpha/bit-depth handling. Use native processing on unsupported input or conversion failure. |
| JPEG/JPG | Try the JXL route. Export preserves the native JPEG settings by staging native output and requiring byte-identical JPEG reconstruction; otherwise use that native output. |
| PPM | Additional binary RGB image import/export through the tools. |
| PGM | Additional binary grayscale import/export. Color export is deliberately converted to grayscale. |
| PNM | Additional binary grayscale/RGB import; RGB export. |
| PAM | Additional integer RGB/grayscale import and RGB/RGBA export. |
| PGX | Additional unsigned integer grayscale import/export. Color export is deliberately converted to grayscale. |
| EXR and other native formats outside the supported bridge | Continue through their existing handlers. Float/HDR/multipass data is not converted through an integer PNG bridge. |
| GIF/APNG and other animated levels | Continue through the existing level handlers. This patch does not replace animation decoding with a still-image conversion. |

Netpbm/PGX entries require the experimental setting. These formats have no
native raster handler in the current OpenToonz base, so a failed conversion
reports an error instead of claiming a native fallback exists. ASCII Netpbm,
bitmap PBM, floating-point PFM, unsupported channel layouts, and unsupported
sample encodings are outside this initial bridge. Concatenated Netpbm images
and files with trailing image data are rejected rather than reading only the
first image.

For formats with native handlers, failed launch, timeout, nonzero exit,
unsupported metadata, missing output, and invalid converted output select the
native path. Fallback reasons are written to diagnostic output. Normal image
operations do not show a warning dialog for a successful native fallback.

## Save and fallback behavior

Experimental exports first create the standard native file with its existing
format properties. The JXL tools process that completed file. If conversion
fails, the exact native file is used. This preserves settings such as JPEG
quality and provides a concrete fallback without replaying image rows.

New JXL and Netpbm/PGX exports use a PNG or grayscale staging image. They have
no native target-format fallback and report conversion failure clearly.

Routed exports publish only a completed file, using an atomic save. An existing
destination is preserved if conversion, copying, or final publication fails.
Unique temporary directories isolate concurrent frame conversions. The tools
receive ASCII relative image filenames so paths containing spaces or Unicode
are handled by Qt's file operations.

This is an alternative processing route, not a promise of faster rendering.
It adds subprocesses and temporary I/O, and inspection/loading can involve a
full conversion. The timeout stops and reaps a stalled subprocess before
temporary files are removed.

## Development and verification

The implementation consists of preference controls, a CLI backend, raster
adapters, and small raster-factory hooks. Native leaf factories handle staging
and fallback directly, avoiding recursive entry into the experimental route.
The existing raster library remains independent of application Preferences;
the `image` library installs the adapters.

Before promoting this exploration, verify it in a packaged Windows build with
the intended tool distribution, including a numbered level, render output,
transparency over a nonblack background, 16-bit output, DPI, disabled settings,
missing tools, and a failed conversion. GUI and background rendering both need
application-level testing. Linux backend tests do not establish Windows/macOS
application compatibility.

AI assistance: implementation and review were assisted by ChatGPT (GPT-6 Astra
Pro), following the request to add external JXL processing and a default-off
experimental route for other supported formats with native fallback.
