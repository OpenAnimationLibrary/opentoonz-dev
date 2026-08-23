# OT-Dev Production Journal references

This directory publishes the maintained, read-only side of the OpenToonz
Production Journal prototype. A local single-file TiddlyWiki loads the current
reference revision from this repository and pairs it with separate local notes.

The initial vertical slice contains eight manual-derived references. They
summarize selected manual areas and link back to the official documentation;
they do not replace the manual.

## Published contract

- `reference-manifest.json` identifies the edition, schema, revision, payload,
  reference count, publication time and SHA-256 checksum.
- `tiddlers/reference-tiddlers.json` is the machine-readable reference payload.
- `tiddlers/site-tiddlers.json` supplies the online wiki title, home page and
  read-only presentation.
- `validate-reference-pack.mjs` rejects unsafe system titles, duplicate IDs,
  incompatible fields, revision drift and checksum drift.
- `tiddlywiki.info` builds the public single-file wiki.

Reference titles may change. `canonical-id` is the stable join key used by local
annotations and formal journal experiments.

## Update rule

Change the reference tiddlers, advance the manifest revision, regenerate the
payload checksum and run:

```sh
node docs/production-journal/validate-reference-pack.mjs
```

The Pages workflow validates the feed and builds the online TiddlyWiki with a
pinned TiddlyWiki release before deployment. Local annotations are not present
in this directory and are never uploaded by the reference loader.
