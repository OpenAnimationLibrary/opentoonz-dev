# OTLUT source integration

Imported from OpenAnimationLibrary/otlut PR #1, commit
`abb684e8af7c1ed712eaed6b5ac986d5af039999` (BSD 3-Clause, see LICENSE).
OpenToonz builds the same core into its Palette to LUT dialog and builds a bundled
standalone `otlut` helper for the existing image-pair workflow.

Local additions: sparse palette fitting with bounded RGB tolerance, preservation
constraints, cancellation and post-fit trilinear validation. The cube writer uses
red-fastest file ordering; internal OTLUT storage remains blue-fastest.

`stb_image.h` is pinned to nothings/stb commit
`31c1ad37456438565541f4919958214b6e762fb4`. Its MIT/public-domain license is
included in that header. No build-time network download is required.
