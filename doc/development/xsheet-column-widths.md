# Individual Xsheet column widths

Choose **Preferences > Xsheet > Column Header Layout > Adjustable** and restart
once when changing the header layout. The other layouts and the horizontal
Timeline retain their existing geometry.

- Drag the right boundary in the header's name/number row to resize one column.
- When that column belongs to the column selection, dragging sets all selected
  columns to the same resulting width. Nonadjacent selections are supported.
- Double-click that boundary, or choose **Fit to Content**, to fit each target
  column independently. Measurements use all populated cells, not just the
  visible frames, and include names, drawing numbers, Note text, and padding.
- **Column Width...** sets a precise width for the column or selected columns.
- **Reset Column Width** restores the default for the column or selection;
  **Reset All Column Widths** restores defaults across the Xsheet.
- Dragging previews the layout. Release commits one undoable change; Escape
  cancels. AutoFit, numeric sizing, and reset are also undoable.

Individual widths accept 50–2048 logical pixels. The inherited default remains
74, or the existing `xsheetColumnWidth` preference (50–200). Manual and AutoFit
widths remain stable when content is edited; run AutoFit again to recalculate.
For multiline Note text, AutoFit measures the longest line. Raster thumbnails
and handwriting do not determine widths from their image dimensions.

Widths belong to the column objects and follow movement and cloning. Replacing
an empty column with a different level type preserves its width. Overrides are
saved per Xsheet, including sub-Xsheets and otherwise empty trailing columns,
in an optional `widths` attribute on the existing `columns` element. Older
readers can ignore the attribute. Reset clears the override rather than copying
the current default into it. Camera widths and Timeline layer heights are not
affected.

Configure with `-DBUILD_XSHEET_COLUMN_WIDTH_TESTS=ON` and run
`ctest --output-on-failure -R '^xsheet_column_width$'`. The regression covers
mixed-width hit detection and folding, cached drawing geometry, column moves,
insert/remove, cloning, type replacement, scene streams, and reset. Windows CI
runs it before the application build. Interactive validation should also cover
resize cursors and grips, selected-column drags, double-click versus renaming,
scrolling, keyframes, waveforms, Escape, undo/redo, and scene reopening.
