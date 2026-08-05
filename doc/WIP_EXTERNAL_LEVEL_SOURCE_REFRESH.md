# External Level Source Refresh (WIP)

## Purpose

OpenToonz may continue displaying already-cached level data after the source file at the same path has been replaced or modified outside the application. The current scene can therefore remain attached to stale decoded images until a broader refresh, scene change, or application restart forces the level to be read again.

This work is intended to make that state visible and deliberate.

The primary feature is source synchronization, not recovery. Retaining the cached version can nevertheless be valuable when the source was replaced unintentionally or when the artist needs time to compare the two versions.

## Intended behavior

When OpenToonz is about to reuse decoded level data, it should compare the current source against the fingerprint recorded when that cached data was accepted.

An unchanged source should continue to load silently from cache.

When the source has changed, OpenToonz should present one decision for the level rather than prompting once per frame, thumbnail, viewer, or Xsheet request:

- **Load Updated File** — invalidate decoded images and thumbnails associated with the level and read the current source.
- **Keep Current Cached Version** — continue using the version already held by the scene for this session.
- **Save Cached Copy...** — when a complete cached level is available, save that retained version to another location before choosing which source remains active.
- **Cancel** — leave the current state unchanged.

The wording should describe synchronization with an externally changed source. Cached-copy saving is a secondary safeguard, not the identity of the feature.

## Source fingerprint

The first implementation should retain:

- canonical decoded source path;
- file size;
- last-modified timestamp;
- level or sequence extent where applicable.

A content hash may be used only when size and timestamp do not provide enough certainty. Hashing every large movie or image sequence during routine access would be unnecessarily expensive.

For image sequences, the implementation must account for changes to individual members and additions or removals from the sequence, not merely changes to the representative path.

## Cache layers that must remain coherent

A source-change decision must cover the complete level rather than only one reader instance. At minimum, the implementation needs to coordinate:

- `TXshSimpleLevel` loaded frame data;
- `TImageCache` entries;
- Level Strip and scene-cast thumbnails;
- viewer and Xsheet image-builder caches;
- format-specific intermediate caches, including FFmpeg-backed animation extraction;
- any retained level metadata whose frame count or dimensions may have changed.

The WebP reader introduced separately in PR #58 already keys its extraction cache by path, size, and modification time. This WIP addresses the higher-level OpenToonz state that can prevent a changed source from reaching that reader at all.

## Prompt suppression and scope

The decision should be recorded per source fingerprint transition so that normal scrubbing and playback do not repeatedly prompt.

Multiple columns referring to the same level should share the same decision.

Choosing **Keep Current Cached Version** should suppress additional prompts for that exact disk version during the current scene session. A subsequent external modification should create a new fingerprint transition and may prompt again.

## Save Cached Copy

This option is available only when OpenToonz can verify that the cached representation is complete enough to save.

The saved form may differ from the original encoded format. For example, a cached movie or animated WebP may be recoverable as an image sequence rather than as a byte-identical copy of the original container.

The UI must not describe cache retention as a guaranteed backup system. Cache entries may be incomplete, evicted, or decoded into a representation that does not preserve all original metadata.

## Initial implementation sequence

1. Locate the authoritative points where external levels are admitted to and reused from the in-memory level/image cache.
2. Add a reusable source-fingerprint type and comparison helper.
3. Record the accepted fingerprint with the loaded simple level.
4. Detect mismatch before returning stale cached frames during explicit reload and level re-addition paths.
5. Implement one consolidated source-change dialog.
6. On **Load Updated File**, invalidate level frames, thumbnails, builders, and format-specific intermediate state before rereading.
7. Add **Keep Current Cached Version** session suppression.
8. Add **Save Cached Copy...** only after complete-cache detection and safe export behavior are established.
9. Extend detection to image sequences and test files replaced with the same size and timestamp granularity.

## Required tests

- Replace a loaded still image at the same path and re-add the level.
- Replace an animated WebP at the same path while its extracted-frame cache exists.
- Replace a movie with another having different frame count and dimensions.
- Modify one member of an image sequence.
- Add and remove frames from an image sequence.
- Confirm one prompt per changed source, not one per frame request.
- Confirm Level Strip thumbnail generation, Xsheet scrubbing, and playback remain usable while the decision is handled.
- Confirm **Keep Current Cached Version** preserves the current scene appearance.
- Confirm **Load Updated File** clears stale frames and thumbnails before loading the replacement.
- Confirm unchanged sources continue to reuse cache without prompts or performance regression.

## Current WIP status

This initial commit establishes the behavioral contract and implementation boundaries. Code changes will follow on this branch after the reload and cache-ownership paths are fully traced.
