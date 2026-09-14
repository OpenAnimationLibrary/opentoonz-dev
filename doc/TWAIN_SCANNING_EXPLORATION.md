# Exploratory TWAIN Scanning Restoration

## Purpose

OpenToonz still contains its legacy TWAIN scanner abstraction and raster handoff code, but scanning is not currently functional in modern Windows builds.

This document defines the first exploratory milestone for restoring scanning without redesigning the Scan & Cleanup workflow.

## First milestone

Restore the smallest useful end-to-end TWAIN path on Windows:

1. Select an installed TWAIN data source from OpenToonz.
2. Open the scanner's native TWAIN user interface.
3. Acquire one image.
4. Transfer the image into OpenToonz through the existing scanner/raster handoff.
5. Complete without crashing or silently failing.

The scanner's native UI should own scan configuration for this first milestone. OpenToonz-side control of DPI, brightness, contrast, threshold, scan area, ADF behavior, and multi-page acquisition can be reconnected incrementally after basic acquisition works.

## Existing OpenToonz path to preserve

The current source tree already provides the higher-level structure needed for acquisition:

- `TScanner` supplies the application-facing scanner abstraction.
- `TScannerTwain` handles TWAIN acquisition and maps returned buffers to OpenToonz rasters.
- The legacy `common/twain` code supplies DSM/session/capability handling.
- `TScannerTwain::onDoneCB()` already converts supported TWAIN pixel formats into `TRasterImageP`, assigns DPI, and reports the image through `notifyImageDone()`.

The initial restoration should therefore focus on the TWAIN DSM/session boundary rather than replacing the downstream OpenToonz scan workflow.

## Reference implementation

Use the TWAIN Working Group's current TWAIN 2.x DSM and sample application as the behavioral reference for:

- DSM loading/opening,
- source enumeration and selection,
- data-source open/close,
- native UI enable/disable,
- TWAIN state transitions,
- transfer-ready notification,
- single-image transfer,
- cleanup after cancellation or error.

The first prototype should prefer the standard source-selection/native-source UI path over attempting to configure every capability programmatically.

## Initial success criterion

A physical or virtual TWAIN data source installed on Windows can be selected from OpenToonz, can perform one scan through its native interface, and can return a valid raster image to OpenToonz.

## Follow-up work

Only after the initial acquisition path is demonstrated should this effort expand into reconnecting OpenToonz's Scan Settings controls, including resolution, image mode, scan area, brightness, contrast, threshold, feeder support, multi-page scanning, cancellation, and scanner-profile persistence.

This is exploratory work and should remain isolated from upstream staging until the basic acquisition path is demonstrated and tested.