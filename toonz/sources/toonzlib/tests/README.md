# Mesh-column empty-buffer regression (#6923)

Configure the normal OpenToonz build with `-DBUILD_XSHEET_TESTS=ON`, build
`xsheet_empty_cells_test`, and run `ctest --output-on-failure -R '^xsheet_empty_cells$'`
from the build directory. On Windows specify the build configuration and provide
the normal runtime DLL search paths.

The test uses real TXsheet, stage objects and Plastic objects across 16
ordinary/mesh, partial/all-exposure, occupied/empty-target and same/different-column
combinations. It checks source object identity, ordinary values, Plastic key
existence, vertex counts, child parenting, center/offset and stage-stream reload.

It calls storage operations directly. It does not run GUI drag tools, CellsMover,
the Undo manager, either column-transfer preference, or a complete .tnz round trip.
It does not compare all Plastic numeric values, reopened edges or destination rig
state. Those integration checks remain manual. Explicit column transfers and
direct-key overwrite/return/cancellation defects are outside this patch.
