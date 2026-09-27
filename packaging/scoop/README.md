# Scoop OpenToonz manifest

`opentoonz-otdev.json` installs a pinned OT-Dev Windows portable release. The published ZIP has `OpenToonz.exe` and a populated `portablestuff` directory at its root. OpenToonz detects portable mode when `portablestuff` exists in its working directory, so the Scoop Start menu shortcut launches the executable from that directory. This avoids the registry-based `TOONZROOT` setup used by the standard Windows installer.

To try this manifest in PowerShell:

```powershell
scoop install https://raw.githubusercontent.com/OpenAnimationLibrary/opentoonz-dev/main/packaging/scoop/opentoonz-otdev.json
```

Until this PR is merged, use the corresponding URL on the PR branch instead of `main`. The app name is `opentoonz-otdev`. Start it from the Scoop shortcut; if launching the EXE manually, first change to the directory returned by `scoop prefix opentoonz-otdev`.

Scoop persists the entire `portablestuff` directory under `scoop/persist/opentoonz-otdev`. Projects, profiles, brushes, and configuration survive an update or ordinary uninstall. `scoop uninstall -p opentoonz-otdev` deletes persisted data. Back up that directory before purging it.

## Updating the pinned release

For each new alpha release, update `version`, `url`, and the SHA-256 `hash` together. Confirm the asset is a ZIP with `OpenToonz.exe` and `portablestuff/config` at its root. Do not point `url` at the mutable `nightly` tag: a Scoop hash must identify an immutable asset.

The first installation seeds `portablestuff` from the release. Scoop keeps the user's persisted copy on subsequent updates. New or changed default resources in later release ZIPs will **not** automatically replace those persisted files; review and merge resource changes explicitly before promoting a new manifest version. The binary and its resource set should be tested together.

This manifest belongs to OT-Dev for validation and release packaging. Submission to ScoopInstaller/Extras would require its own review and should not replace the official installer-based `opentoonz` package without agreement from that project.
